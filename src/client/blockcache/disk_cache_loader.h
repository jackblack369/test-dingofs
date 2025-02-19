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
 * Created Date: 2024-08-20
 * Author: Jingli Chen (Wine93)
 */

#ifndef DINGOFS_SRC_CLIENT_BLOCKCACHE_DISK_CACHE_LOADER_H_
#define DINGOFS_SRC_CLIENT_BLOCKCACHE_DISK_CACHE_LOADER_H_

#include <atomic>
#include <memory>
#include <string>

#include "client/blockcache/disk_cache_layout.h"
#include "client/blockcache/disk_cache_manager.h"
#include "client/blockcache/disk_cache_metric.h"
#include "client/blockcache/local_filesystem.h"
#include "utils/concurrent/task_thread_pool.h"

namespace dingofs {
namespace client {
namespace blockcache {

using ::dingofs::utils::TaskThreadPool;
using FileInfo = LocalFileSystem::FileInfo;
using UploadFunc = CacheStore::UploadFunc;

class DiskCacheLoader {
  enum class BlockType {
    STAGE_BLOCK,
    CACHE_BLOCK,
  };

 public:
  DiskCacheLoader(std::shared_ptr<DiskCacheLayout> layout,
                  std::shared_ptr<LocalFileSystem> fs,
                  std::shared_ptr<DiskCacheManager> manager,
                  std::shared_ptr<DiskCacheMetric> metric);

  virtual ~DiskCacheLoader() = default;

  virtual void Start(const std::string& disk_id, UploadFunc uploader);

  virtual void Stop();

  virtual bool IsLoading();

 private:
  void LoadAllBlocks(const std::string& root, BlockType type);

  bool LoadOneBlock(const std::string& prefix, const FileInfo& file,
                    BlockType type);

  std::string ToString(BlockType type);

 private:
  std::string disk_id_;
  UploadFunc uploader_;
  std::atomic<bool> running_;
  std::shared_ptr<DiskCacheLayout> layout_;
  std::shared_ptr<LocalFileSystem> fs_;
  std::shared_ptr<DiskCacheManager> manager_;
  std::shared_ptr<DiskCacheMetric> metric_;
  std::unique_ptr<TaskThreadPool<>> task_pool_;
};

}  // namespace blockcache
}  // namespace client
}  // namespace dingofs

#endif  // DINGOFS_SRC_CLIENT_BLOCKCACHE_DISK_CACHE_LOADER_H_
