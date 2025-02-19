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

#ifndef DINGOFS_SRC_MDS_SCHEDULE_OPERATORFACTORYTEMPLATE_H_
#define DINGOFS_SRC_MDS_SCHEDULE_OPERATORFACTORYTEMPLATE_H_

#include <chrono>
#include <memory>

#include "mds/schedule/operatorStepTemplate.h"
#include "mds/schedule/operatorTemplate.h"

using ::std::chrono::steady_clock;
namespace dingofs {
namespace mds {
namespace schedule {
template <class IdType, class CopySetInfoT, class CopySetConfT>
class OperatorFactoryT {
 public:
  using Operator = OperatorT<IdType, CopySetInfoT, CopySetConfT>;
  using OperatorStep = OperatorStepT<IdType, CopySetInfoT, CopySetConfT>;
  using AddPeer = AddPeerT<IdType, CopySetInfoT, CopySetConfT>;
  using ChangePeer = ChangePeerT<IdType, CopySetInfoT, CopySetConfT>;
  using TransferLeader = TransferLeaderT<IdType, CopySetInfoT, CopySetConfT>;
  using RemovePeer = RemovePeerT<IdType, CopySetInfoT, CopySetConfT>;
  using ScanPeer = ScanPeerT<IdType, CopySetInfoT, CopySetConfT>;

 public:
  /**
   * @brief Create_xxxx_Operator: Create operator for xxxx
   */
  Operator CreateTransferLeaderOperator(const CopySetInfoT& info,
                                        IdType newLeader, OperatorPriority pri);

  Operator CreateRemovePeerOperator(const CopySetInfoT& info, IdType rmPeer,
                                    OperatorPriority pri);

  Operator CreateAddPeerOperator(const CopySetInfoT& info, IdType addPeer,
                                 OperatorPriority pri);

  Operator CreateChangePeerOperator(const CopySetInfoT& info, IdType rmPeer,
                                    IdType addPeer, OperatorPriority pri);

  Operator CreateScanPeerOperator(const CopySetInfoT& info,
                                  IdType startScanPeer, OperatorPriority pri,
                                  ConfigChangeType opType);
};

// extern OperatorFactoryT<ChunkServerIdType, CopySetInfo, CopySetConf>
//     operatorFactory;

template <class IdType, class CopySetInfoT, class CopySetConfT>
OperatorT<IdType, CopySetInfoT, CopySetConfT> OperatorFactoryT<
    IdType, CopySetInfoT,
    CopySetConfT>::CreateTransferLeaderOperator(const CopySetInfoT& info,
                                                IdType newLeader,
                                                OperatorPriority pri) {
  return Operator(info.epoch, info.id, pri, steady_clock::now(),
                  std::make_shared<TransferLeader>(info.leader, newLeader));
}

template <class IdType, class CopySetInfoT, class CopySetConfT>
OperatorT<IdType, CopySetInfoT, CopySetConfT>
OperatorFactoryT<IdType, CopySetInfoT, CopySetConfT>::CreateRemovePeerOperator(
    const CopySetInfoT& info, IdType peer, OperatorPriority pri) {
  return Operator(info.epoch, info.id, pri, steady_clock::now(),
                  std::make_shared<RemovePeer>(peer));
}

template <class IdType, class CopySetInfoT, class CopySetConfT>
OperatorT<IdType, CopySetInfoT, CopySetConfT>
OperatorFactoryT<IdType, CopySetInfoT, CopySetConfT>::CreateAddPeerOperator(
    const CopySetInfoT& info, IdType addPeer, OperatorPriority pri) {
  return Operator(info.epoch, info.id, pri, steady_clock::now(),
                  std::make_shared<AddPeer>(addPeer));
}

template <class IdType, class CopySetInfoT, class CopySetConfT>
OperatorT<IdType, CopySetInfoT, CopySetConfT>
OperatorFactoryT<IdType, CopySetInfoT, CopySetConfT>::CreateChangePeerOperator(
    const CopySetInfoT& info, IdType rmPeer, IdType addPeer,
    OperatorPriority pri) {
  return Operator(info.epoch, info.id, pri, steady_clock::now(),
                  std::make_shared<ChangePeer>(rmPeer, addPeer));
}

template <class IdType, class CopySetInfoT, class CopySetConfT>
OperatorT<IdType, CopySetInfoT, CopySetConfT>
OperatorFactoryT<IdType, CopySetInfoT, CopySetConfT>::CreateScanPeerOperator(
    const CopySetInfoT& info, IdType scanPeer, OperatorPriority pri,
    ConfigChangeType opType) {
  return Operator(info.epoch, info.id, pri, steady_clock::now(),
                  std::make_shared<ScanPeer>(scanPeer, opType));
}

}  // namespace schedule
}  // namespace mds
}  // namespace dingofs
#endif  // DINGOFS_SRC_MDS_SCHEDULE_OPERATORFACTORYTEMPLATE_H_
