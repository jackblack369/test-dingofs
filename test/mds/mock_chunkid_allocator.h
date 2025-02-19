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
 * @Project: dingo
 * @Date: 2021-09-06
 * @Author: chengyi
 */
#ifndef DINGOFS_TEST_MDS_MOCK_CHUNKID_ALLOCATOR_H_
#define DINGOFS_TEST_MDS_MOCK_CHUNKID_ALLOCATOR_H_

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "mds/chunkid_allocator.h"

namespace dingofs {
namespace mds {

class MockChunkIdAllocatorImpl : public ChunkIdAllocatorImpl {
 public:
  MOCK_METHOD(int, GenChunkId, (uint64_t idNum, uint64_t* chunkId), (override));

  MOCK_METHOD(void, Init,
              (const std::shared_ptr<KVStorageClient>& client,
               const std::string& chunkIdStoreKey, uint64_t bundleSize),
              (override));

  MOCK_METHOD(int, AllocateBundleIds, (int bundleSize), (override));
};
}  // namespace mds
}  // namespace dingofs
#endif  // DINGOFS_TEST_MDS_MOCK_CHUNKID_ALLOCATOR_H_
