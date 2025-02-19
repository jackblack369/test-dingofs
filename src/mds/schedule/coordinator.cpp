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
 * @Date: 2021-11-8 11:01:48
 * @Author: chenwei
 */

#include "mds/schedule/coordinator.h"

#include <glog/logging.h>

#include <memory>
#include <string>
#include <utility>

#include "mds/common/mds_define.h"
#include "mds/schedule/operatorFactory.h"
#include "mds/topology/deal_peerid.h"
#include "mds/topology/topology_item.h"

namespace dingofs {
namespace mds {
namespace schedule {
/**
 * use curl -L mdsIp:port/flags/enableRecoverScheduler?setvalue=true
 * for dynamic parameter configuration
 */
static bool PassBool(const char*, bool) { return true; }
DEFINE_bool(enableRecoverScheduler, true, "switch of recover scheduler");
DEFINE_validator(enableRecoverScheduler, &PassBool);
DEFINE_bool(enableCopySetScheduler, true, "switch of copyset scheduler");
DEFINE_validator(enableCopySetScheduler, &PassBool);
DEFINE_bool(enableLeaderScheduler, true, "switch of leader scheduler");
DEFINE_validator(enableLeaderScheduler, &PassBool);

Coordinator::Coordinator(const std::shared_ptr<TopoAdapter>& topo) {
  this->topo_ = topo;
}

Coordinator::~Coordinator() { Stop(); }

void Coordinator::InitScheduler(const ScheduleOption& conf,
                                std::shared_ptr<ScheduleMetrics> metrics) {
  conf_ = conf;

  opController_ =
      std::make_shared<OperatorController>(conf.operatorConcurrent, metrics);

  if (conf.enableRecoverScheduler) {
    schedulerController_[SchedulerType::RecoverSchedulerType] =
        std::make_shared<RecoverScheduler>(conf, topo_, opController_);
    LOG(INFO) << "init recover scheduler ok!";
  }

  if (conf.enableCopysetScheduler) {
    schedulerController_[SchedulerType::CopysetSchedulerType] =
        std::make_shared<CopySetScheduler>(conf, topo_, opController_);
    LOG(INFO) << "init copyset scheduler ok!";
  }

  if (conf.enableLeaderScheduler) {
    schedulerController_[SchedulerType::LeaderSchedulerType] =
        std::make_shared<LeaderScheduler>(conf, topo_, opController_);
    LOG(INFO) << "init leader scheduler ok!";
  }
}

void Coordinator::Run() {
  for (auto& v : schedulerController_) {
    runSchedulerThreads_[v.first] = dingofs::utils::Thread(
        &Coordinator::RunScheduler, this, std::cref(v.second), v.first);
  }
}

void Coordinator::Stop() {
  sleeper_.interrupt();
  for (auto& v : schedulerController_) {
    if (runSchedulerThreads_.find(v.first) == runSchedulerThreads_.end()) {
      continue;
    }
    runSchedulerThreads_[v.first].join();
    runSchedulerThreads_.erase(v.first);
  }
}

MetaServerIdType Coordinator::CopySetHeartbeat(
    const mds::topology::CopySetInfo& origin_info,
    const pb::mds::heartbeat::ConfigChangeInfo& config_ch_info,
    pb::mds::heartbeat::CopySetConf* out) {
  // transfer copyset info format from topology to scheduler
  CopySetInfo info;
  if (!topo_->CopySetFromTopoToSchedule(origin_info, &info)) {
    LOG(ERROR) << "coordinator cannot convert copyset("
               << origin_info.GetPoolId() << "," << origin_info.GetId()
               << ") from heartbeat topo form to schedule form error";
    return mds::topology::UNINITIALIZE_ID;
  }
  info.configChangeInfo = config_ch_info;

  // check if there's any operator on specified copyset
  Operator op;
  if (!opController_->GetOperatorById(info.id, &op)) {
    return mds::topology::UNINITIALIZE_ID;
  }
  LOG(INFO) << "find operator on" << info.CopySetInfoStr()
            << "), operator: " << op.OpToString();

  // Update the status of the operator according to the copyset information
  // reported by the leader, return true if there's any new configuration
  CopySetConf res;
  bool has_order = opController_->ApplyOperator(info, &res);
  if (has_order) {
    LOG(INFO) << "going to order operator " << op.OpToString();
    // determine whether the epoch and startEpoch are the same,
    // if not, the operator will not be dispatched
    // scenario: The MDS has already dispatch the operator, and the
    //           copyset has finished but not yet report. At this time
    //           the MDS restart and generate new operator on this copyset.
    //           this operator should not be dispatched and should be
    //           removed
    if (info.epoch != op.startEpoch) {
      LOG(WARNING) << "Operator " << op.OpToString() << "on "
                   << info.CopySetInfoStr() << " is stale, remove operator";
      opController_->RemoveOperator(info.id);
      return mds::topology::UNINITIALIZE_ID;
    }

    // the operator should not be dispacthed if the candidate
    // of addPeer or transferLeader or changePeer is offline
    MetaServerInfo meta_server;
    if (!topo_->GetMetaServerInfo(res.configChangeItem, &meta_server)) {
      LOG(ERROR) << "coordinator can not get metaServer "
                 << res.configChangeItem << " from topology";
      opController_->RemoveOperator(info.id);
      return mds::topology::UNINITIALIZE_ID;
    }
    bool need_check_type = (res.type == ConfigChangeType::ADD_PEER ||
                            res.type == ConfigChangeType::TRANSFER_LEADER ||
                            res.type == ConfigChangeType::CHANGE_PEER);
    if (need_check_type && meta_server.IsOffline()) {
      LOG(WARNING) << "candidate metaserver " << meta_server.info.id
                   << " is offline, abort config change";
      opController_->RemoveOperator(info.id);
      return mds::topology::UNINITIALIZE_ID;
    }

    // build the copysetConf need to be returned in heartbeat
    // if build failed, remove the operator
    if (!BuildCopySetConf(res, out)) {
      LOG(ERROR) << "build copyset conf for " << info.CopySetInfoStr()
                 << ") fail, remove operator";
      opController_->RemoveOperator(info.id);
      return mds::topology::UNINITIALIZE_ID;
    }

    LOG(INFO) << "order operator " << op.OpToString() << " on "
              << info.CopySetInfoStr() << " success";
    return res.configChangeItem;
  }

  return mds::topology::UNINITIALIZE_ID;
}

pb::mds::schedule::ScheduleStatusCode Coordinator::QueryMetaServerRecoverStatus(
    const std::vector<MetaServerIdType>& id_list,
    std::map<MetaServerIdType, bool>* status_map) {
  std::vector<MetaServerInfo> infos;

  // if idList is empty, get all metaserver info
  if (id_list.empty()) {
    infos = topo_->GetMetaServerInfos();
  } else {
    for (auto id : id_list) {
      MetaServerInfo info;
      bool get_ok = topo_->GetMetaServerInfo(id, &info);
      if (!get_ok) {
        LOG(ERROR) << "invalid metaserver id: " << id;
        return pb::mds::schedule::ScheduleStatusCode::InvalidQueryMetaserverID;
      }
      infos.emplace_back(std::move(info));
    }
  }

  // Iterate to check whether each metaserver is recovering
  // recovering: metaserver offline but has recover task on it
  for (const MetaServerInfo& info : infos) {
    (*status_map)[info.info.id] = IsMetaServerRecover(info);
  }

  return pb::mds::schedule::ScheduleStatusCode::Success;
}

void Coordinator::RunScheduler(const std::shared_ptr<Scheduler>& s,
                               SchedulerType type) {
  while (sleeper_.wait_for(std::chrono::seconds(s->GetRunningInterval()))) {
    if (ScheduleNeedRun(type)) {
      s->Schedule();
    }
  }
  LOG(INFO) << ScheduleName(type) << " exit.";
}

bool Coordinator::BuildCopySetConf(const CopySetConf& res,
                                   pb::mds::heartbeat::CopySetConf* out) {
  // build the copysetConf need to be returned in heartbeat
  out->set_poolid(res.id.first);
  out->set_copysetid(res.id.second);
  out->set_epoch(res.epoch);
  out->set_type(res.type);

  // set candidate
  MetaServerInfo meta_server;
  if (!topo_->GetMetaServerInfo(res.configChangeItem, &meta_server)) {
    LOG(ERROR) << "coordinator can not get metaServer " << res.configChangeItem
               << " from topology";
    return false;
  }

  auto* replica = new pb::common::Peer();
  replica->set_id(res.configChangeItem);
  replica->set_address(topology::BuildPeerIdWithIpPort(
      meta_server.info.ip, meta_server.info.port, 0));
  out->set_allocated_configchangeitem(replica);

  // set old
  if (res.oldOne != mds::topology::UNINITIALIZE_ID) {
    if (!topo_->GetMetaServerInfo(res.oldOne, &meta_server)) {
      LOG(ERROR) << "coordinator can not get metaServer " << res.oldOne
                 << " from topology";
      return false;
    }

    auto* replica = new pb::common::Peer();
    replica->set_id(res.oldOne);
    replica->set_address(topology::BuildPeerIdWithIpPort(
        meta_server.info.ip, meta_server.info.port, 0));
    out->set_allocated_oldpeer(replica);
  }

  // set peers
  for (const auto& peer : res.peers) {
    auto* replica = out->add_peers();
    replica->set_id(peer.id);
    replica->set_address(
        topology::BuildPeerIdWithIpPort(peer.ip, peer.port, 0));
  }

  return true;
}

bool Coordinator::MetaserverGoingToAdd(MetaServerIdType ms_id, CopySetKey key) {
  Operator op;
  // no operator on copyset
  if (!opController_->GetOperatorById(key, &op)) {
    return false;
  }

  // the operator type is 'add' and new metaserver = msId
  AddPeer* res = dynamic_cast<AddPeer*>(op.step.get());
  LOG(INFO) << "find operator " << op.OpToString();
  if (res != nullptr && ms_id == res->GetTargetPeer()) {
    LOG(INFO) << "metaserver " << ms_id << " is target of pending operator "
              << op.OpToString();
    return true;
  }

  // the operator type is 'change' and target = msId
  ChangePeer* cres = dynamic_cast<ChangePeer*>(op.step.get());
  LOG(INFO) << "find operator " << op.OpToString();
  if (cres != nullptr && ms_id == cres->GetTargetPeer()) {
    LOG(INFO) << "metaserver " << ms_id << " is target of pending operator "
              << op.OpToString();
    return true;
  }

  return false;
}

bool Coordinator::ScheduleNeedRun(SchedulerType type) {
  switch (type) {
    case SchedulerType::RecoverSchedulerType:
      return FLAGS_enableRecoverScheduler;
    case SchedulerType::CopysetSchedulerType:
      return FLAGS_enableCopySetScheduler;
    case SchedulerType::LeaderSchedulerType:
      return FLAGS_enableLeaderScheduler;
    default:
      return false;
  }
}

std::string Coordinator::ScheduleName(SchedulerType type) {
  switch (type) {
    case SchedulerType::RecoverSchedulerType:
      return "RecoverScheduler";
    case SchedulerType::CopysetSchedulerType:
      return "CopySetScheduler";
    case SchedulerType::LeaderSchedulerType:
      return "LeaderScheduler";
    default:
      return "Unknown";
  }
}

std::shared_ptr<OperatorController> Coordinator::GetOpController() {
  return opController_;
}

bool Coordinator::IsMetaServerRecover(const MetaServerInfo& info) {
  // Non-offline state, it will not be recovered
  if (!info.IsOffline()) {
    return false;
  }

  // if the metaserver is offline, check if there's any corresponding high
  // priority changePeer task
  std::vector<Operator> op_list = opController_->GetOperators();
  for (Operator& op : op_list) {
    if (op.priority != OperatorPriority::HighPriority) {
      continue;
    }

    auto* instance = dynamic_cast<ChangePeer*>(op.step.get());
    if (instance == nullptr) {
      continue;
    }

    if (instance->GetOldPeer() == info.info.id) {
      return true;
    }
  }

  // check if there's any migrating copyset on the metaserver
  std::vector<CopySetInfo> copyset_infos =
      topo_->GetCopySetInfosInMetaServer(info.info.id);
  for (CopySetInfo& cs_info : copyset_infos) {
    if (cs_info.configChangeInfo.type() == ConfigChangeType::CHANGE_PEER) {
      return true;
    }
  }

  return false;
}

}  // namespace schedule
}  // namespace mds
}  // namespace dingofs
