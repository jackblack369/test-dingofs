/*
 *  Copyright (c) 2020 NetEase Inc.
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

/*************************************************************************
> File Name: mock_s3_adapter.h
> Author:
> Created Time: Tue 7 Sept 2021
 ************************************************************************/

#ifndef DINGOFS_TEST_METASERVER_S3COMPACT_MOCK_S3_ADAPTER_H_
#define DINGOFS_TEST_METASERVER_S3COMPACT_MOCK_S3_ADAPTER_H_

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>

#include "aws/s3_adapter.h"
#include "metaserver/s3compact_manager.h"

using ::dingofs::aws::S3Adapter;
using ::testing::Return;

namespace dingofs {
namespace metaserver {

class MockS3AdapterManager : public S3AdapterManager {
 public:
  MockS3AdapterManager(uint64_t size, const aws::S3AdapterOption& opts)
      : S3AdapterManager(size, opts) {}
  ~MockS3AdapterManager() override = default;
  MOCK_METHOD0(Init, void());
  MOCK_METHOD0(GetS3Adapter, std::pair<uint64_t, S3Adapter*>());
  MOCK_METHOD1(ReleaseS3Adapter, void(uint64_t));
};

class MockS3Adapter : public S3Adapter {
 public:
  MockS3Adapter() = default;
  ~MockS3Adapter() override = default;

  MOCK_METHOD1(Init, void(const std::string&));
  MOCK_METHOD0(Deinit, void());
  MOCK_METHOD1(Reinit, void(const aws::S3AdapterOption& opt));
  MOCK_METHOD0(GetS3Ak, std::string());
  MOCK_METHOD0(GetS3Sk, std::string());
  MOCK_METHOD0(GetS3Endpoint, std::string());
  MOCK_METHOD0(GetBucketName, std::string());
  MOCK_METHOD2(PutObject, int(const Aws::String&, const std::string&));
  MOCK_METHOD2(GetObject, int(const Aws::String&, std::string*));
  MOCK_METHOD1(DeleteObject, int(const Aws::String&));
};
}  // namespace metaserver
}  // namespace dingofs
#endif  // DINGOFS_TEST_METASERVER_S3COMPACT_MOCK_S3_ADAPTER_H_
