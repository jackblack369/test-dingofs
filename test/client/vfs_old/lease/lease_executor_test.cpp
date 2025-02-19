/*
 *  Copyright (c) 2022 NetEase Inc.
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
 * Created Date: Wed Mar 31 2022
 * Author: lixiaocui
 */

#include <brpc/server.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <gtest/gtest.h>

#include "client/vfs_old/lease/lease_excutor.h"
#include "stub/rpcclient/mock_mds_client.h"
#include "stub/rpcclient/mock_metacache.h"

using dingofs::pb::mds::topology::PartitionTxId;
using dingofs::stub::rpcclient::MockMdsClient;
using dingofs::stub::rpcclient::MockMetaCache;

using ::testing::AtLeast;
using ::testing::Return;
using ::testing::SetArgPointee;

namespace dingofs {
namespace client {

class LeaseExecutorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    mdsCli_ = std::make_shared<MockMdsClient>();
    metaCache_ = std::make_shared<MockMetaCache>();
  }

  void TearDown() override {}

 protected:
  std::shared_ptr<MockMdsClient> mdsCli_;
  std::shared_ptr<MockMetaCache> metaCache_;
  common::LeaseOpt opt_;
};

TEST_F(LeaseExecutorTest, test_start) {
  {
    LOG(INFO) << "### case1: invalid lease time ###";
    opt_.leaseTimeUs = 0;
    std::atomic<bool> enableSumInDir;
    enableSumInDir.store(true);
    LeaseExecutor exec(opt_, metaCache_, mdsCli_, &enableSumInDir);
    ASSERT_FALSE(exec.Start());
  }

  {
    LOG(INFO) << "### case2: invalid refresh times per lease ###";
    opt_.refreshTimesPerLease = 0;
    opt_.leaseTimeUs = 20;
    std::atomic<bool> enableSumInDir;
    enableSumInDir.store(true);
    LeaseExecutor exec(opt_, metaCache_, mdsCli_, &enableSumInDir);
    ASSERT_FALSE(exec.Start());
  }
}

TEST_F(LeaseExecutorTest, test_start_stop) {
  opt_.leaseTimeUs = 100000;  // 100ms
  opt_.refreshTimesPerLease = 5;

  // prepare mock action
  PartitionTxId txid;
  txid.set_partitionid(1);
  txid.set_txid(2);
  std::vector<PartitionTxId> txIds = {txid};
  EXPECT_CALL(*metaCache_, GetAllTxIds(_))
      .WillOnce(SetArgPointee<0>(std::vector<PartitionTxId>{}))
      .WillRepeatedly(SetArgPointee<0>(txIds));
  EXPECT_CALL(*mdsCli_, RefreshSession(_, _, _, _, _))
      .WillOnce(Return(pb::mds::FSStatusCode::UNKNOWN_ERROR))
      .WillRepeatedly(testing::DoAll(SetArgPointee<1>(txIds),
                                     Return(pb::mds::FSStatusCode::OK)));
  EXPECT_CALL(*metaCache_, SetTxId(1, 2)).Times(AtLeast(1));

  // lease executor start
  std::atomic<bool> enableSumInDir;
  enableSumInDir.store(true);
  LeaseExecutor exec(opt_, metaCache_, mdsCli_, &enableSumInDir);
  ASSERT_TRUE(exec.Start());

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  ASSERT_NO_FATAL_FAILURE(exec.Stop());

  // multi stoo ok
  ASSERT_NO_FATAL_FAILURE(exec.Stop());
}

}  // namespace client
}  // namespace dingofs
