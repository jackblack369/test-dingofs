/*
 *  Copyright (c) 2021 NetEase Inc.
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
 * Created Date: 2021-08-27
 * Author: chengyi01
 */

#ifndef DINGOFS_SRC_MDS_CHUNKID_ALLOCATOR_H_
#define DINGOFS_SRC_MDS_CHUNKID_ALLOCATOR_H_

#include <memory>
#include <string>

#include "mds/common/storage_key.h"
#include "mds/kvstorageclient/etcd_client.h"
#include "utils/concurrent/concurrent.h"

namespace dingofs {
namespace mds {

const uint64_t CHUNKIDINITIALIZE = 0;
const uint64_t CHUNKBUNDLEALLOCATED = 1000;

class ChunkIdAllocator {
 public:
  ChunkIdAllocator() {}
  virtual ~ChunkIdAllocator() {}
  /**
   * @brief Generate a ID
   *
   * @param chunkId
   * @return int
   * @details
   */
  virtual int GenChunkId(uint64_t idNum, uint64_t* chunkId) = 0;

  /**
   * @brief init ChunkIdAllocator
   *
   * @param client etcd client
   * @param chunkIdStoreKey
   * @param bundleSize
   * @details
   */
  virtual void Init(
      const std::shared_ptr<kvstorage::KVStorageClient>& client = nullptr,
      const std::string& chunkIdStoreKey = CHUNKID_NAME_KEY_PREFIX,
      uint64_t bundleSize = CHUNKBUNDLEALLOCATED) = 0;
};

class ChunkIdAllocatorImpl : public ChunkIdAllocator {
 public:
  ChunkIdAllocatorImpl(
      std::shared_ptr<kvstorage::KVStorageClient> client = nullptr,
      std::string storeKey = CHUNKID_NAME_KEY_PREFIX,
      uint64_t initId = CHUNKIDINITIALIZE,
      uint64_t bundleSize = CHUNKBUNDLEALLOCATED)
      : ChunkIdAllocator(),
        client_(client),
        storeKey_(storeKey),
        nextId_(initId),
        lastId_(initId),
        bundleSize_(bundleSize) {}

  virtual ~ChunkIdAllocatorImpl() {}

  /**
   * @brief Generate a globally incremented ID
   *
   * @param chunkId
   * @return int
   * @details
   */
  int GenChunkId(uint64_t idNum, uint64_t* chunkId) override;

  /**
   * @brief init ChunkIdAllocator
   *
   * @param client
   * @param chunkIdStoreKey
   * @param bundleSize
   * @details
   * init ChunkIdAllocator, use it for init or change some configuration.
   * but this class object will work as old configuration,
   * until the chunkIds in the current bundle is exhausted.
   */
  virtual void Init(
      const std::shared_ptr<kvstorage::KVStorageClient>& client = nullptr,
      const std::string& chunkIdStoreKey = CHUNKID_NAME_KEY_PREFIX,
      uint64_t bundleSize = CHUNKBUNDLEALLOCATED) override;
  /**
   * @brief get bundleSize chunkIds from etcd
   *
   * @param bundleSize get the number of chunkIds
   * @return int
   * 0:   ok or key not exist
   * -1:  unknow error
   * -2:  value decodes fails
   * -3:  CAS error
   * @details
   */
  virtual int AllocateBundleIds(int bundleSize);

  static bool DecodeID(const std::string& value, uint64_t* out);

  static std::string EncodeID(uint64_t value) { return std::to_string(value); }
  enum ChunkIdAllocatorStatusCode {
    KEY_NOTEXIST = 1,
    OK = 0,
    UNKNOWN_ERROR = -1,
    DECODE_ERROR = -2,
    CAS_ERROR = -3
  };

 private:
  std::shared_ptr<kvstorage::KVStorageClient> client_;  // the etcd client
  std::string storeKey_;  // the key of ChunkId stored in etcd
  uint64_t nextId_;       // the next ChunkId can be allocated in this bunlde
  uint64_t lastId_;       // the last ChunkId can be allocated in this bunlde
  uint64_t bundleSize_;   // get the numnber of ChunkId at a time
  utils::RWLock nextIdRWlock_;  // guarantee the uniqueness of the ChunkId
};

}  // namespace mds
}  // namespace dingofs

#endif  // DINGOFS_SRC_MDS_CHUNKID_ALLOCATOR_H_
