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
 * @Date: 2021-11-15 11:01:48
 * @Author: chenwei
 */

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "mds/schedule/common.h"

namespace dingofs {
namespace mds {
namespace schedule {
TEST(OperatorStepTest, OperatorStepTest_TransferLeader_Test) {
  auto originCopySetInfo = GetCopySetInfoForTest();
  std::shared_ptr<OperatorStep> transferLeader =
      std::make_shared<TransferLeader>(1, 2);

  auto testCopySetInfo = originCopySetInfo;
  ApplyStatus applyStatus;
  CopySetConf copySetConf;

  {
    // 1. transfer leader haven't started
    applyStatus = transferLeader->Apply(originCopySetInfo, &copySetConf);
    ASSERT_EQ(ApplyStatus::Ordered, applyStatus);
    ASSERT_EQ(ConfigChangeType::TRANSFER_LEADER, copySetConf.type);
    ASSERT_EQ(2, copySetConf.configChangeItem);
  }

  {
    // 2. transfer leader complete
    testCopySetInfo.leader = 2;
    ASSERT_EQ(ApplyStatus::Finished,
              transferLeader->Apply(testCopySetInfo, &copySetConf));
  }

  {
    // 3. report leader is not record old/target leader in operator
    testCopySetInfo.leader = 3;
    ASSERT_EQ(ApplyStatus::Failed,
              transferLeader->Apply(testCopySetInfo, &copySetConf));
  }

  {
    // 4. transfer leader fail
    testCopySetInfo.leader = 1;
    CandidateError* candidateError = new CandidateError();
    std::string* errMsg = new std::string("transfer leader err");
    candidateError->set_allocated_errmsg(errMsg);
    candidateError->set_errtype(1);
    testCopySetInfo.candidatePeerInfo = PeerInfo(2, 1, 1, "", 9000);
    testCopySetInfo.configChangeInfo.set_finished(false);
    testCopySetInfo.configChangeInfo.set_type(
        ConfigChangeType::TRANSFER_LEADER);
    auto replica = new ::dingofs::common::Peer();
    replica->set_id(4);
    replica->set_address("192.10.12.4:9000:0");
    testCopySetInfo.configChangeInfo.set_allocated_peer(replica);
    testCopySetInfo.configChangeInfo.set_allocated_err(candidateError);
    ASSERT_EQ(ApplyStatus::Failed,
              transferLeader->Apply(testCopySetInfo, &copySetConf));
  }

  {
    // 5. transfer leader report not complete
    testCopySetInfo.leader = 1;
    testCopySetInfo.candidatePeerInfo = PeerInfo(2, 1, 1, "", 9000);
    testCopySetInfo.configChangeInfo.set_finished(false);
    auto replica = new ::dingofs::common::Peer();
    replica->set_id(5);
    replica->set_address("192.10.12.5:9000:0");
    testCopySetInfo.configChangeInfo.set_allocated_peer(replica);
    testCopySetInfo.configChangeInfo.release_err();
    ASSERT_EQ(ApplyStatus::OnGoing,
              transferLeader->Apply(testCopySetInfo, &copySetConf));
  }

  {
    // 6. tarnfer leader type not complete
    testCopySetInfo.configChangeInfo.set_type(ConfigChangeType::ADD_PEER);
    ASSERT_EQ(ApplyStatus::Failed,
              transferLeader->Apply(testCopySetInfo, &copySetConf));
  }

  {
    // 7. transfer leader report not complete and candidate not match
    testCopySetInfo.candidatePeerInfo = PeerInfo(3, 1, 1, "", 9000);
    auto replica = new ::dingofs::common::Peer();
    replica->set_id(6);
    replica->set_address("192.10.12.6:9000");
    testCopySetInfo.configChangeInfo.set_type(
        ConfigChangeType::TRANSFER_LEADER);
    testCopySetInfo.configChangeInfo.set_allocated_peer(replica);
    ASSERT_EQ(ApplyStatus::Failed,
              transferLeader->Apply(testCopySetInfo, &copySetConf));
  }
}

TEST(OperatorSepTest, OperatorSepTest_AddPeer_Test) {
  auto originCopySetInfo = GetCopySetInfoForTest();
  std::shared_ptr<OperatorStep> addPeer = std::make_shared<AddPeer>(4);

  // 1. add peer haven't started
  CopySetConf copySetConf;
  ASSERT_EQ(ApplyStatus::Ordered,
            addPeer->Apply(originCopySetInfo, &copySetConf));
  ASSERT_EQ(4, copySetConf.configChangeItem);
  ASSERT_EQ(ConfigChangeType::ADD_PEER, copySetConf.type);

  // 2. add peer complete
  auto testCopySetInfo = originCopySetInfo;
  testCopySetInfo.peers.emplace_back(PeerInfo(4, 3, 4, "192.168.10.4", 9000));
  ASSERT_EQ(ApplyStatus::Finished,
            addPeer->Apply(testCopySetInfo, &copySetConf));

  // 3. add peer fail
  testCopySetInfo = originCopySetInfo;
  testCopySetInfo.candidatePeerInfo = PeerInfo(4, 1, 1, "", 9000);
  auto replica = new ::dingofs::common::Peer();
  replica->set_id(4);
  replica->set_address("192.10.12.4:9000:0");
  testCopySetInfo.configChangeInfo.set_allocated_peer(replica);
  testCopySetInfo.configChangeInfo.set_type(ConfigChangeType::ADD_PEER);
  testCopySetInfo.configChangeInfo.set_finished(false);
  std::string* errMsg = new std::string("add peer failed");
  CandidateError* candidateError = new CandidateError();
  candidateError->set_errtype(2);
  candidateError->set_allocated_errmsg(errMsg);
  testCopySetInfo.configChangeInfo.set_allocated_err(candidateError);
  ASSERT_EQ(ApplyStatus::Failed, addPeer->Apply(testCopySetInfo, &copySetConf));

  // 4. add peer report not complete
  testCopySetInfo.configChangeInfo.set_finished(false);
  testCopySetInfo.configChangeInfo.release_err();
  ASSERT_EQ(ApplyStatus::OnGoing,
            addPeer->Apply(testCopySetInfo, &copySetConf));

  // 5. add peer type not complete
  testCopySetInfo.configChangeInfo.set_type(ConfigChangeType::REMOVE_PEER);
  ASSERT_EQ(ApplyStatus::Failed, addPeer->Apply(testCopySetInfo, &copySetConf));

  // 6. config change item do not match
  testCopySetInfo.configChangeInfo.set_type(ConfigChangeType::ADD_PEER);
  testCopySetInfo.configChangeInfo.set_finished(true);
  testCopySetInfo.candidatePeerInfo = PeerInfo(5, 1, 1, "", 9000);
  replica = new ::dingofs::common::Peer();
  replica->set_id(5);
  replica->set_address("192.10.12.5:9000:0");
  testCopySetInfo.configChangeInfo.set_allocated_peer(replica);
  ASSERT_EQ(ApplyStatus::Failed, addPeer->Apply(testCopySetInfo, &copySetConf));
}

TEST(OperatorStepTest, OperatorStepTest_RemovePeer_Test) {
  auto originCopySetInfo = GetCopySetInfoForTest();
  std::shared_ptr<OperatorStep> removePeer = std::make_shared<RemovePeer>(3);

  // 1. remove peer haven't started
  CopySetConf copySetConf;
  ASSERT_EQ(ApplyStatus::Ordered,
            removePeer->Apply(originCopySetInfo, &copySetConf));
  ASSERT_EQ(3, copySetConf.configChangeItem);
  ASSERT_EQ(ConfigChangeType::REMOVE_PEER, copySetConf.type);

  // 2. remove peer complete
  auto testCopySetInfo = originCopySetInfo;
  auto removeIndex = testCopySetInfo.peers.end() - 1;
  testCopySetInfo.peers.erase(removeIndex);
  ASSERT_EQ(ApplyStatus::Finished,
            removePeer->Apply(testCopySetInfo, &copySetConf));

  // 3. remove peer failed
  testCopySetInfo = originCopySetInfo;
  testCopySetInfo.candidatePeerInfo = PeerInfo(3, 1, 1, "", 9000);
  auto replica = new ::dingofs::common::Peer();
  replica->set_id(4);
  replica->set_address("192.10.12.4:9000:0");
  testCopySetInfo.configChangeInfo.set_allocated_peer(replica);
  testCopySetInfo.configChangeInfo.set_type(ConfigChangeType::REMOVE_PEER);
  std::string* errMsg = new std::string("remove peer err");
  CandidateError* candidateError = new CandidateError();
  candidateError->set_errtype(2);
  candidateError->set_allocated_errmsg(errMsg);
  testCopySetInfo.configChangeInfo.set_finished(false);
  testCopySetInfo.configChangeInfo.set_allocated_err(candidateError);
  ASSERT_EQ(ApplyStatus::Failed,
            removePeer->Apply(testCopySetInfo, &copySetConf));

  // 4. remove peer report not complete
  testCopySetInfo.configChangeInfo.set_finished(false);
  testCopySetInfo.configChangeInfo.release_err();
  ASSERT_EQ(ApplyStatus::OnGoing,
            removePeer->Apply(testCopySetInfo, &copySetConf));

  // 5. remove peer type not complete
  testCopySetInfo.configChangeInfo.set_type(ConfigChangeType::ADD_PEER);
  ASSERT_EQ(ApplyStatus::Failed,
            removePeer->Apply(testCopySetInfo, &copySetConf));

  // 5. config change item do not match
  testCopySetInfo.candidatePeerInfo = PeerInfo(10, 1, 1, "", 9000);
  replica = new ::dingofs::common::Peer();
  replica->set_id(9);
  replica->set_address("192.168.10.1:9000:0");
  testCopySetInfo.configChangeInfo.set_allocated_peer(replica);
  testCopySetInfo.configChangeInfo.set_finished(true);
  testCopySetInfo.configChangeInfo.set_type(ConfigChangeType::REMOVE_PEER);
  ASSERT_EQ(ApplyStatus::Failed,
            removePeer->Apply(testCopySetInfo, &copySetConf));
}

TEST(OperatorStepTest, OperatorStepTest_ChangePeer_Test) {
  auto originCopySetInfo = GetCopySetInfoForTest();
  std::shared_ptr<OperatorStep> changePeer = std::make_shared<ChangePeer>(3, 4);

  CopySetConf copySetConf;
  // 1. change peer还未开始
  {
    ASSERT_EQ(ApplyStatus::Ordered,
              changePeer->Apply(originCopySetInfo, &copySetConf));
    ASSERT_EQ(4, copySetConf.configChangeItem);
    ASSERT_EQ(3, copySetConf.oldOne);
    ASSERT_EQ(ConfigChangeType::CHANGE_PEER, copySetConf.type);
  }

  auto testCopySetInfo = originCopySetInfo;
  // 2. change peer完成
  {
    auto testCopySetInfo = originCopySetInfo;
    testCopySetInfo.peers.erase(testCopySetInfo.peers.begin() + 2);
    testCopySetInfo.peers.emplace_back(PeerInfo(4, 3, 4, "192.168.10.4", 9000));
    ASSERT_EQ(ApplyStatus::Finished,
              changePeer->Apply(testCopySetInfo, &copySetConf));
  }

  // 3. change peer失败
  {
    testCopySetInfo = originCopySetInfo;
    testCopySetInfo.candidatePeerInfo = PeerInfo(4, 1, 1, "", 9000);
    auto replica = new ::dingofs::common::Peer();
    replica->set_id(4);
    replica->set_address("192.10.12.4:9000:0");
    testCopySetInfo.configChangeInfo.set_allocated_peer(replica);
    testCopySetInfo.configChangeInfo.set_type(ConfigChangeType::CHANGE_PEER);
    testCopySetInfo.configChangeInfo.set_finished(false);
    std::string* errMsg = new std::string("add peer failed");
    CandidateError* candidateError = new CandidateError();
    candidateError->set_errtype(2);
    candidateError->set_allocated_errmsg(errMsg);
    testCopySetInfo.configChangeInfo.set_allocated_err(candidateError);
    ASSERT_EQ(ApplyStatus::Failed,
              changePeer->Apply(testCopySetInfo, &copySetConf));
  }

  // 4. 上报未完成
  {
    testCopySetInfo.configChangeInfo.set_finished(false);
    testCopySetInfo.configChangeInfo.release_err();
    ASSERT_EQ(ApplyStatus::OnGoing,
              changePeer->Apply(testCopySetInfo, &copySetConf));
  }

  // 5. 上报的变更类型和mds中的oprator不相符合
  {
    testCopySetInfo.configChangeInfo.set_type(ConfigChangeType::ADD_PEER);
    testCopySetInfo.configChangeInfo.set_finished(true);
    testCopySetInfo.candidatePeerInfo = PeerInfo(5, 1, 1, "", 9000);
    auto replica = new ::dingofs::common::Peer();
    replica->set_id(5);
    replica->set_address("192.10.12.5:9000:0");
    testCopySetInfo.configChangeInfo.set_allocated_peer(replica);
    ASSERT_EQ(ApplyStatus::Failed,
              changePeer->Apply(testCopySetInfo, &copySetConf));
  }
}
}  // namespace schedule
}  // namespace mds
}  // namespace dingofs
