/*
 *  Copyright (c) 2023 NetEase Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

/*
 * Project: dingo
 * Created Date: 2023-01-31
 * Author: chengyi01
 */

#ifndef DINGOFS_SRC_CLIENT_WARMUP_WARMUP_MANAGER_H_
#define DINGOFS_SRC_CLIENT_WARMUP_WARMUP_MANAGER_H_

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "aws/s3_adapter.h"
#include "client/vfs_old/common/common.h"
#include "client/vfs_old/dentry_cache_manager.h"
#include "client/vfs_old/inode_cache_manager.h"
#include "client/vfs/vfs.h"
#include "client/vfs_old/kvclient/kvclient_manager.h"
#include "client/vfs_old/s3/client_s3_adaptor.h"
#include "common/task_thread_pool.h"
#include "stub/metric/metric.h"
#include "stub/rpcclient/metaserver_client.h"
#include "utils/concurrent/concurrent.h"
#include "utils/concurrent/rw_lock.h"

namespace dingofs {
namespace client {
namespace warmup {

using ThreadPool = dingofs::common::TaskThreadPool2<bthread::Mutex,
                                                    bthread::ConditionVariable>;

class WarmupFile {
 public:
  explicit WarmupFile(fuse_ino_t key = 0, uint64_t file_len = 0)
      : key_(key), fileLen_(file_len) {}

  fuse_ino_t GetKey() const { return key_; }
  uint64_t GetFileLen() const { return fileLen_; }
  bool operator==(const WarmupFile& other) const { return key_ == other.key_; }

 private:
  fuse_ino_t key_;
  uint64_t fileLen_;
};

using WarmupFilelist = WarmupFile;

class WarmupInodes {
 public:
  explicit WarmupInodes(fuse_ino_t key = 0,
                        std::set<fuse_ino_t> list = std::set<fuse_ino_t>())
      : key_(key), readAheadFiles_(std::move(list)) {}

  fuse_ino_t GetKey() const { return key_; }
  const std::set<fuse_ino_t>& GetReadAheadFiles() const {
    return readAheadFiles_;
  }

  void AddFileInode(fuse_ino_t file) { readAheadFiles_.emplace(file); }

 private:
  fuse_ino_t key_;
  std::set<fuse_ino_t> readAheadFiles_;
};

class WarmupProgress {
 public:
  explicit WarmupProgress(
      common::WarmupStorageType type =
          common::WarmupStorageType::kWarmupStorageTypeUnknown)
      : total_(0), finished_(0), error_(0), storageType_(type) {}

  WarmupProgress(const WarmupProgress& wp)
      : total_(wp.total_),
        finished_(wp.finished_),
        error_(wp.error_),
        storageType_(wp.storageType_) {}

  void AddTotal(uint64_t add) {
    std::lock_guard<std::mutex> lock(totalMutex_);
    total_ += add;
  }

  WarmupProgress& operator=(const WarmupProgress& wp) {
    total_ = wp.total_;
    finished_ = wp.finished_;
    error_ = wp.error_;
    return *this;
  }

  void FinishedPlusOne() {
    std::lock_guard<std::mutex> lock(finishedMutex_);
    ++finished_;
  }

  uint64_t GetTotal() {
    std::lock_guard<std::mutex> lock(totalMutex_);
    return total_;
  }

  uint64_t GetFinished() {
    std::lock_guard<std::mutex> lock(finishedMutex_);
    return finished_;
  }

  void ErrorsPlusOne() {
    std::lock_guard<std::mutex> lock(errorMutex_);
    ++error_;
  }

  uint64_t GetErrors() {
    std::lock_guard<std::mutex> lock(errorMutex_);
    return error_;
  }

  std::string ToString() {
    std::lock_guard<std::mutex> lockT(totalMutex_);
    std::lock_guard<std::mutex> lockF(finishedMutex_);
    std::lock_guard<std::mutex> lockE(errorMutex_);
    return "total:" + std::to_string(total_) +
           ",finished:" + std::to_string(finished_) +
           ",error:" + std::to_string(error_);
  }

  common::WarmupStorageType GetStorageType() { return storageType_; }

 private:
  uint64_t total_;
  std::mutex totalMutex_;
  uint64_t finished_;
  std::mutex finishedMutex_;
  common::WarmupStorageType storageType_;
  uint64_t error_;  // TODO may be better to use atomic types
  std::mutex errorMutex_;
};

class WarmupManager {
 public:
  WarmupManager()
      : mounted_(false),
        metaClient_(std::make_shared<stub::rpcclient::MetaServerClientImpl>()),
        inodeManager_(std::make_shared<InodeCacheManagerImpl>(metaClient_)),
        dentryManager_(std::make_shared<DentryCacheManagerImpl>(metaClient_)) {
    kvClientManager_ = nullptr;
  }

  explicit WarmupManager(
      std::shared_ptr<stub::rpcclient::MetaServerClient> meta_client,
      std::shared_ptr<InodeCacheManager> inode_manager,
      std::shared_ptr<DentryCacheManager> dentry_manager,
      std::shared_ptr<pb::mds::FsInfo> fs_info,
      std::shared_ptr<KVClientManager> kv_client_manager, vfs::VFS* vfs)
      : mounted_(false),
        metaClient_(std::move(meta_client)),
        inodeManager_(std::move(inode_manager)),
        dentryManager_(std::move(dentry_manager)),
        fsInfo_(std::move(fs_info)),
        kvClientManager_(std::move(kv_client_manager)),
        vfs_(vfs) {}

  virtual void Init(const common::FuseClientOption& option) {
    option_ = option;
  }
  virtual void UnInit() { ClearWarmupProcess(); }

  virtual bool AddWarmupFilelist(fuse_ino_t key,
                                 common::WarmupStorageType type) = 0;
  virtual bool AddWarmupFile(fuse_ino_t key, const std::string& path,
                             common::WarmupStorageType type) = 0;

  void SetMounted(bool mounted) {
    mounted_.store(mounted, std::memory_order_release);
  }

  void SetFsInfo(const std::shared_ptr<pb::mds::FsInfo>& fsinfo) {
    fsInfo_ = fsinfo;
  }

  /**
   * @brief
   *
   * @param key
   * @param progress
   * @return true
   * @return false no this warmup task or finished
   */
  bool QueryWarmupProgress(fuse_ino_t key, WarmupProgress* progress) {
    bool ret = true;
    utils::ReadLockGuard lock(inode2ProgressMutex_);
    auto iter = FindWarmupProgressByKeyLocked(key);
    if (iter != inode2Progress_.end()) {
      *progress = iter->second;
    } else {
      ret = false;
    }
    return ret;
  }

  void CollectMetrics(stub::metric::InterfaceMetric* interface, int count,
                      uint64_t start);

 protected:
  /**
   * @brief Add warmupProcess
   *
   * @return true
   * @return false warmupProcess has been added
   */
  virtual bool AddWarmupProcess(fuse_ino_t key,
                                common::WarmupStorageType type) {
    utils::WriteLockGuard lock(inode2ProgressMutex_);
    auto ret = inode2Progress_.emplace(key, WarmupProgress(type));
    return ret.second;
  }

  /**
   * @brief
   * Please use it with the lock inode2ProgressMutex_
   * @param key
   * @return std::unordered_map<fuse_ino_t, WarmupProgress>::iterator
   */
  std::unordered_map<fuse_ino_t, WarmupProgress>::iterator
  FindWarmupProgressByKeyLocked(fuse_ino_t key) {
    return inode2Progress_.find(key);
  }

  virtual void ClearWarmupProcess() {
    utils::WriteLockGuard lock(inode2ProgressMutex_);
    inode2Progress_.clear();
  }

 protected:
  std::atomic<bool> mounted_;

  // metaserver client
  std::shared_ptr<stub::rpcclient::MetaServerClient> metaClient_;

  // inode cache manager
  std::shared_ptr<InodeCacheManager> inodeManager_;

  // dentry cache manager
  std::shared_ptr<DentryCacheManager> dentryManager_;

  // filesystem info
  std::shared_ptr<pb::mds::FsInfo> fsInfo_;

  // warmup progress
  std::unordered_map<fuse_ino_t, WarmupProgress> inode2Progress_;
  utils::BthreadRWLock inode2ProgressMutex_;

  std::shared_ptr<KVClientManager> kvClientManager_ = nullptr;

  common::FuseClientOption option_;

  vfs::VFS* vfs_;
};

class WarmupManagerS3Impl : public WarmupManager {
 public:
  explicit WarmupManagerS3Impl(
      std::shared_ptr<stub::rpcclient::MetaServerClient> meta_client,
      std::shared_ptr<InodeCacheManager> inode_manager,
      std::shared_ptr<DentryCacheManager> dentry_manager,
      std::shared_ptr<pb::mds::FsInfo> fs_info,
      std::shared_ptr<S3ClientAdaptor> s3_adaptor,
      std::shared_ptr<KVClientManager> kv_client_manager, vfs::VFS* vfs)
      : WarmupManager(std::move(meta_client), std::move(inode_manager),
                      std::move(dentry_manager), std::move(fs_info),
                      std::move(kv_client_manager), vfs),
        s3Adaptor_(std::move(s3_adaptor)) {}

  bool AddWarmupFilelist(fuse_ino_t key,
                         common::WarmupStorageType type) override;
  bool AddWarmupFile(fuse_ino_t key, const std::string& path,
                     common::WarmupStorageType type) override;

  void Init(const common::FuseClientOption& option) override;
  void UnInit() override;

 private:
  void BackGroundFetch();

  void GetWarmupList(const WarmupFilelist& filelist,
                     std::vector<std::string>* list);

  void FetchDentryEnqueue(fuse_ino_t key, const std::string& file);

  void LookPath(fuse_ino_t key, std::string file);

  void FetchDentry(fuse_ino_t key, fuse_ino_t ino, const std::string& file);

  void FetchChildDentry(fuse_ino_t key, fuse_ino_t ino);

  /**
   * @brief
   * Please use it with the lock warmupInodesDequeMutex_
   * @param key
   * @return std::deque<WarmupInodes>::iterator
   */
  std::deque<WarmupInodes>::iterator FindWarmupInodesByKeyLocked(
      fuse_ino_t key) {
    return std::find_if(
        warmupInodesDeque_.begin(), warmupInodesDeque_.end(),
        [key](const WarmupInodes& inodes) { return key == inodes.GetKey(); });
  }

  /**
   * @brief
   * Please use it with the lock warmupFilelistDequeMutex_
   * @param key
   * @return std::deque<WarmupFilelist>::iterator
   */
  std::deque<WarmupFilelist>::iterator FindWarmupFilelistByKeyLocked(
      fuse_ino_t key) {
    return std::find_if(warmupFilelistDeque_.begin(),
                        warmupFilelistDeque_.end(),
                        [key](const WarmupFilelist& filelist_) {
                          return key == filelist_.GetKey();
                        });
  }

  /**
   * @brief
   * Please use it with the lock inode2FetchDentryPoolMutex_
   * @param key
   * @return std::unordered_map<fuse_ino_t,
   * std::unique_ptr<ThreadPool>>::iterator
   */
  std::unordered_map<fuse_ino_t, std::unique_ptr<ThreadPool>>::iterator
  FindFetchDentryPoolByKeyLocked(fuse_ino_t key) {
    return inode2FetchDentryPool_.find(key);
  }

  /**
   * @brief
   * Please use it with the lock inode2FetchS3ObjectsPoolMutex_
   * @param key
   * @return std::unordered_map<fuse_ino_t,
   * std::unique_ptr<ThreadPool>>::iterator
   */
  std::unordered_map<fuse_ino_t, std::unique_ptr<ThreadPool>>::iterator
  FindFetchS3ObjectsPoolByKeyLocked(fuse_ino_t key) {
    return inode2FetchS3ObjectsPool_.find(key);
  }

  void FetchDataEnqueue(fuse_ino_t key, fuse_ino_t ino);

  using S3ChunkInfoMapType =
      google::protobuf::Map<uint64_t, pb::metaserver::S3ChunkInfoList>;

  // travel all chunks
  void TravelChunks(fuse_ino_t key, fuse_ino_t ino,
                    const S3ChunkInfoMapType& s3_chunk_info_map);

  using ObjectListType = std::list<std::pair<blockcache::BlockKey, uint64_t>>;
  // travel and download all objs belong to the chunk
  void TravelChunk(fuse_ino_t ino,
                   const pb::metaserver::S3ChunkInfoList& chunk_info,
                   ObjectListType* prefetch_objs);

  // warmup all the prefetchObjs
  void WarmUpAllObjs(fuse_ino_t ino,
                     const std::list<std::pair<blockcache::BlockKey, uint64_t>>&
                         prefetch_objs);

  /**
   * @brief Whether the warmup task[key] is completed (or terminated)
   *
   * @return true
   * @return false
   */
  bool ProgressDone(fuse_ino_t key);

  void ScanCleanFetchDentryPool();

  void ScanCleanFetchS3ObjectsPool();

  void ScanCleanWarmupProgress();

  void ScanWarmupInodes();

  void ScanWarmupFilelist();

  void AddFetchDentryTask(fuse_ino_t key, std::function<void()> task);

  void AddFetchS3objectsTask(fuse_ino_t key, std::function<void()> task);

  void PutObjectToCache(
      fuse_ino_t ino,
      const std::shared_ptr<aws::GetObjectAsyncContext>& context);

 protected:
  std::deque<WarmupFilelist> warmupFilelistDeque_;
  mutable utils::RWLock warmupFilelistDequeMutex_;

  bool initbgFetchThread_ = false;
  utils::Thread bgFetchThread_;
  std::atomic<bool> bgFetchStop_;

  // TODO(chengyi01): limit thread nums
  std::unordered_map<fuse_ino_t, std::unique_ptr<ThreadPool>>
      inode2FetchDentryPool_;
  mutable utils::RWLock inode2FetchDentryPoolMutex_;

  std::deque<WarmupInodes> warmupInodesDeque_;
  mutable utils::RWLock warmupInodesDequeMutex_;

  // s3 adaptor
  std::shared_ptr<S3ClientAdaptor> s3Adaptor_;

  // TODO(chengyi01): limit thread nums
  std::unordered_map<fuse_ino_t, std::unique_ptr<ThreadPool>>
      inode2FetchS3ObjectsPool_;
  mutable utils::RWLock inode2FetchS3ObjectsPoolMutex_;

  dingofs::stub::metric::WarmupManagerS3Metric warmupS3Metric_;
};

}  // namespace warmup
}  // namespace client
}  // namespace dingofs

#endif  // DINGOFS_SRC_CLIENT_WARMUP_WARMUP_MANAGER_H_
