/*
 * Copyright (c) 2024 dingodb.com, Inc. All Rights Reserved
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Project: DingoFS
 * Created Date: 2024-08-19
 * Author: Jingli Chen (Wine93)
 */

#include "client/blockcache/disk_cache.h"

#include <glog/logging.h>

#include <memory>

#include "absl/cleanup/cleanup.h"
#include "base/string/string.h"
#include "base/time/time.h"
#include "client/blockcache/cache_store.h"
#include "client/blockcache/disk_cache_layout.h"
#include "client/blockcache/disk_cache_manager.h"
#include "client/blockcache/disk_cache_metric.h"
#include "client/blockcache/error.h"
#include "client/blockcache/log.h"
#include "client/blockcache/phase_timer.h"
#include "stub/metric/metric.h"

namespace dingofs {
namespace client {
namespace blockcache {

using ::dingofs::base::string::GenUuid;
using ::dingofs::base::string::TrimSpace;
using ::dingofs::base::time::TimeNow;

using DiskCacheTotalMetric = ::dingofs::stub::metric::DiskCacheMetric;
using DiskCacheMetricGuard =
    ::dingofs::client::blockcache::DiskCacheMetricGuard;

BlockReaderImpl::BlockReaderImpl(int fd, std::shared_ptr<LocalFileSystem> fs)
    : fd_(fd), fs_(fs) {}

BCACHE_ERROR BlockReaderImpl::ReadAt(off_t offset, size_t length,
                                     char* buffer) {
  return fs_->Do([&](const std::shared_ptr<PosixFileSystem> posix) {
    BCACHE_ERROR rc;
    DiskCacheMetricGuard guard(
        &rc, &DiskCacheTotalMetric::GetInstance().read_disk, length);
    rc = posix->LSeek(fd_, offset, SEEK_SET);
    if (rc == BCACHE_ERROR::OK) {
      rc = posix->Read(fd_, buffer, length);
    }
    return rc;
  });
}

void BlockReaderImpl::Close() {
  fs_->Do([&](const std::shared_ptr<PosixFileSystem> posix) {
    posix->Close(fd_);
    return BCACHE_ERROR::OK;
  });
}

DiskCache::DiskCache(DiskCacheOption option)
    : option_(option), running_(false), use_direct_write_(false) {
  metric_ = std::make_shared<DiskCacheMetric>(option);
  layout_ = std::make_shared<DiskCacheLayout>(option.cache_dir);
  disk_state_machine_ = std::make_shared<DiskStateMachineImpl>(metric_);
  disk_state_health_checker_ =
      std::make_unique<DiskStateHealthChecker>(layout_, disk_state_machine_);
  fs_ = std::make_shared<LocalFileSystem>(disk_state_machine_);
  manager_ = std::make_shared<DiskCacheManager>(option.cache_size, layout_, fs_,
                                                metric_);
  loader_ = std::make_unique<DiskCacheLoader>(layout_, fs_, manager_, metric_);
}

BCACHE_ERROR DiskCache::Init(UploadFunc uploader) {
  if (running_.exchange(true)) {
    return BCACHE_ERROR::OK;  // already running
  }

  auto rc = CreateDirs();
  if (rc == BCACHE_ERROR::OK) {
    rc = LoadLockFile();
  }
  if (rc != BCACHE_ERROR::OK) {
    return rc;
  }

  uploader_ = uploader;
  metric_->Init();   // For restart
  DetectDirectIO();  // Detect filesystem whether support direct IO, filesystem
                     // like tmpfs (/dev/shm) will not support it.
  disk_state_machine_->Start();         // monitor disk state
  disk_state_health_checker_->Start();  // probe disk health
  manager_->Start();                    // manage disk capacity, cache expire
  loader_->Start(uuid_, uploader);      // load stage and cache block
  metric_->SetUuid(uuid_);
  metric_->SetRunningStatus(kCacheUp);

  LOG(INFO) << "Disk cache (dir=" << GetRootDir() << ") is up.";
  return BCACHE_ERROR::OK;
}

BCACHE_ERROR DiskCache::Shutdown() {
  if (!running_.exchange(false)) {
    return BCACHE_ERROR::OK;
  }

  LOG(INFO) << "Disk cache (dir=" << GetRootDir() << ") is shutting down...";

  loader_->Stop();
  manager_->Stop();
  disk_state_health_checker_->Stop();
  disk_state_machine_->Stop();
  metric_->SetRunningStatus(kCacheDown);

  LOG(INFO) << "Disk cache (dir=" << GetRootDir() << ") is down.";
  return BCACHE_ERROR::OK;
}

BCACHE_ERROR DiskCache::Stage(const BlockKey& key, const Block& block,
                              BlockContext ctx) {
  BCACHE_ERROR rc;
  PhaseTimer timer;
  auto metric_guard = ::absl::MakeCleanup([&] {
    if (rc == BCACHE_ERROR::OK) {
      metric_->AddStageBlock(1);
    } else {
      metric_->AddStageSkip();
    }
  });
  LogGuard log([&]() {
    return StrFormat("stage(%s,%d): %s%s", key.Filename(), block.size,
                     StrErr(rc), timer.ToString());
  });

  rc = Check(WANT_EXEC | WANT_STAGE);
  if (rc != BCACHE_ERROR::OK) {
    return rc;
  }

  timer.NextPhase(Phase::WRITE_FILE);
  std::string stage_path(GetStagePath(key));
  std::string cache_path(GetCachePath(key));
  rc = fs_->WriteFile(stage_path, block.data, block.size, use_direct_write_);
  if (rc != BCACHE_ERROR::OK) {
    return rc;
  }

  timer.NextPhase(Phase::LINK);
  rc = fs_->HardLink(stage_path, cache_path);
  if (rc == BCACHE_ERROR::OK) {
    timer.NextPhase(Phase::CACHE_ADD);
    manager_->Add(key, CacheValue(block.size, TimeNow()));
  } else {
    LOG(WARNING) << "Link " << stage_path << " to " << cache_path
                 << " failed: " << StrErr(rc);
    rc = BCACHE_ERROR::OK;  // ignore link error
  }

  timer.NextPhase(Phase::ENQUEUE_UPLOAD);
  uploader_(key, stage_path, ctx);
  return rc;
}

BCACHE_ERROR DiskCache::RemoveStage(const BlockKey& key, BlockContext ctx) {
  BCACHE_ERROR rc;
  auto metric_guard = ::absl::MakeCleanup([&] {
    if (rc == BCACHE_ERROR::OK) {
      metric_->AddStageBlock(-1);
    }
  });
  LogGuard log([&]() {
    return StrFormat("removestage(%s): %s", key.Filename(), StrErr(rc));
  });

  // NOTE: we will try to delete stage file even if the disk cache
  //       is down or unhealthy, so we remove the Check(...) here.
  rc = fs_->RemoveFile(GetStagePath(key));
  return rc;
}

BCACHE_ERROR DiskCache::Cache(const BlockKey& key, const Block& block) {
  BCACHE_ERROR rc;
  PhaseTimer timer;
  LogGuard log([&]() {
    return StrFormat("cache(%s,%d): %s%s", key.Filename(), block.size,
                     StrErr(rc), timer.ToString());
  });

  rc = Check(WANT_EXEC | WANT_CACHE);
  if (rc != BCACHE_ERROR::OK) {
    return rc;
  }

  timer.NextPhase(Phase::WRITE_FILE);
  rc = fs_->WriteFile(GetCachePath(key), block.data, block.size);
  if (rc != BCACHE_ERROR::OK) {
    return rc;
  }

  timer.NextPhase(Phase::CACHE_ADD);
  manager_->Add(key, CacheValue(block.size, TimeNow()));
  return rc;
}

BCACHE_ERROR DiskCache::Load(const BlockKey& key,
                             std::shared_ptr<BlockReader>& reader) {
  BCACHE_ERROR rc;
  PhaseTimer timer;
  auto metric_guard = ::absl::MakeCleanup([&] {
    if (rc == BCACHE_ERROR::OK) {
      metric_->AddCacheHit();
    } else {
      metric_->AddCacheMiss();
    }
  });
  LogGuard log([&]() {
    return StrFormat("load(%s): %s%s", key.Filename(), StrErr(rc),
                     timer.ToString());
  });

  rc = Check(WANT_EXEC);
  if (rc != BCACHE_ERROR::OK) {
    return rc;
  } else if (!IsCached(key)) {
    return BCACHE_ERROR::NOT_FOUND;
  }

  timer.NextPhase(Phase::OPEN_FILE);
  rc = fs_->Do([&](const std::shared_ptr<PosixFileSystem> posix) {
    int fd;
    auto rc = posix->Open(GetCachePath(key), O_RDONLY, &fd);
    if (rc == BCACHE_ERROR::OK) {
      reader = std::make_shared<BlockReaderImpl>(fd, fs_);
    }
    return rc;
  });

  // Delete corresponding key of block which maybe already deleted by accident.
  if (rc == BCACHE_ERROR::NOT_FOUND) {
    manager_->Delete(key);
  }
  return rc;
}

bool DiskCache::IsCached(const BlockKey& key) {
  CacheValue value;
  std::string cache_path = GetCachePath(key);
  auto rc = manager_->Get(key, &value);
  if (rc == BCACHE_ERROR::OK) {
    return true;
  } else if (loader_->IsLoading() && fs_->FileExists(cache_path)) {
    return true;
  }
  return false;
}

std::string DiskCache::Id() { return uuid_; }

BCACHE_ERROR DiskCache::CreateDirs() {
  std::vector<std::string> dirs{
      layout_->GetRootDir(),
      layout_->GetStageDir(),
      layout_->GetCacheDir(),
      layout_->GetProbeDir(),
  };

  for (const auto& dir : dirs) {
    auto rc = fs_->MkDirs(dir);
    if (rc != BCACHE_ERROR::OK) {
      return rc;
    }
  }
  return BCACHE_ERROR::OK;
}

BCACHE_ERROR DiskCache::LoadLockFile() {
  size_t length;
  std::shared_ptr<char> buffer;
  auto lock_path = layout_->GetLockPath();
  auto rc = fs_->ReadFile(lock_path, buffer, &length);
  if (rc == BCACHE_ERROR::OK) {
    uuid_ = TrimSpace(std::string(buffer.get(), length));
  } else if (rc == BCACHE_ERROR::NOT_FOUND) {
    uuid_ = GenUuid();
    rc = fs_->WriteFile(lock_path, uuid_.c_str(), uuid_.size());
  }
  return rc;
}

void DiskCache::DetectDirectIO() {
  std::string filepath = layout_->GetDetectPath();
  auto rc = fs_->Do([filepath](const std::shared_ptr<PosixFileSystem> posix) {
    int fd;
    auto rc = posix->Create(filepath, &fd, true);
    posix->Close(fd);
    posix->Unlink(filepath);
    return rc;
  });

  if (rc == BCACHE_ERROR::OK) {
    use_direct_write_ = true;
    LOG(INFO) << "The filesystem of disk cache (dir=" << layout_->GetRootDir()
              << ") supports direct IO.";
  } else {
    use_direct_write_ = false;
    LOG(INFO) << "The filesystem of disk cache (dir=" << layout_->GetRootDir()
              << ") not support direct IO, using buffer IO, detect rc = " << rc;
  }
  metric_->SetUseDirectWrite(use_direct_write_);
}

// Check cache status:
//   1. check running status (UP/DOWN)
//   2. check disk healthy (HEALTHY/UNHEALTHY)
//   3. check disk free space (FULL or NOT)
BCACHE_ERROR DiskCache::Check(uint8_t want) {
  if (!running_.load(std::memory_order_relaxed)) {
    return BCACHE_ERROR::CACHE_DOWN;
  }

  if ((want & WANT_EXEC) && !IsHealthy()) {
    return BCACHE_ERROR::CACHE_UNHEALTHY;
  } else if ((want & WANT_STAGE) && StageFull()) {
    return BCACHE_ERROR::CACHE_FULL;
  } else if ((want & WANT_CACHE) && CacheFull()) {
    return BCACHE_ERROR::CACHE_FULL;
  }
  return BCACHE_ERROR::OK;
}

bool DiskCache::IsLoading() const { return loader_->IsLoading(); }

bool DiskCache::IsHealthy() const {
  return disk_state_machine_->GetDiskState() == DiskState::kDiskStateNormal;
}

bool DiskCache::StageFull() const { return manager_->StageFull(); }

bool DiskCache::CacheFull() const { return manager_->CacheFull(); }

std::string DiskCache::GetRootDir() const { return layout_->GetRootDir(); }

std::string DiskCache::GetStagePath(const BlockKey& key) const {
  return layout_->GetStagePath(key);
}

std::string DiskCache::GetCachePath(const BlockKey& key) const {
  return layout_->GetCachePath(key);
}

}  // namespace blockcache
}  // namespace client
}  // namespace dingofs
