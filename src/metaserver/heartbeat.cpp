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
 * Created Date: 2021-09-12
 * Author: chenwei
 */

#include "metaserver/heartbeat.h"

#include <braft/closure_helper.h>
#include <brpc/channel.h>
#include <brpc/controller.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <unistd.h>

#include <utility>

#include "dingofs/common.pb.h"
#include "metaserver/copyset/copyset_node.h"
#include "metaserver/copyset/utils.h"
#include "metaserver/resource_statistic.h"
#include "utils/timeutility.h"
#include "utils/uri_parser.h"

namespace dingofs {
namespace metaserver {

using copyset::CopysetNode;
using metaserver::copyset::ToGroupIdString;

using pb::common::Peer;
using pb::mds::heartbeat::ConfigChangeInfo;
using pb::mds::heartbeat::ConfigChangeType;
using pb::mds::heartbeat::CopySetConf;
using pb::mds::heartbeat::MetaServerSpaceStatus;

using HeartbeatRequest = pb::mds::heartbeat::MetaServerHeartbeatRequest;
using HeartbeatResponse = pb::mds::heartbeat::MetaServerHeartbeatResponse;

namespace {

int GatherCopysetConfChange(CopysetNode* node, ConfigChangeInfo* info) {
  ConfigChangeType type;
  Peer peer;

  node->GetConfChange(&type, &peer);

  if (type == ConfigChangeType::NONE) {
    return 1;
  }

  *info->mutable_peer() = std::move(peer);
  info->set_type(type);
  info->set_finished(false);
  return 0;
}

std::string CopysetName(const CopySetConf& conf) {
  return ToGroupIdString(conf.poolid(), conf.copysetid());
}

Peer EndPointToPeer(const butil::EndPoint& ep) {
  Peer peer;
  peer.set_address(butil::endpoint2str(ep).c_str() + std::string(":0"));

  return peer;
}

}  // namespace

int Heartbeat::Init(const HeartbeatOptions& options) {
  toStop_.store(false, std::memory_order_release);
  options_ = options;

  std::string copyset_data_path =
      utils::UriParser::GetPathFromUri(options_.storeUri);
  // get the metaserver data dir, because copysets dir doesn't exist at
  // beginning  // NOLINT
  auto path_list = utils::UriParser::ParseDirPath(copyset_data_path);
  if (path_list.size() > 1) {
    auto it = path_list.end();
    std::advance(it, -2);
    storePath_ = *it;
  } else {
    LOG(ERROR) << "Get storePath faild.";
    return -1;
  }

  butil::ip_t ms_ip;
  if (butil::str2ip(options_.ip.c_str(), &ms_ip) < 0) {
    LOG(ERROR) << "Invalid Metaserver IP provided: " << options_.ip;
    return -1;
  }
  msEp_ = butil::EndPoint(ms_ip, options_.port);
  LOG(INFO) << "Metaserver address: " << options_.ip << ":" << options_.port;

  // mdsEps can not empty
  utils::SplitString(options_.mdsListenAddr, ",", &mdsEps_);
  if (mdsEps_.empty()) {
    LOG(ERROR) << "Invalid mds ip provided: " << options_.mdsListenAddr;
    return -1;
  }

  // Check the legitimacy of each address
  for (const auto& addr : mdsEps_) {
    butil::EndPoint endpt;
    if (butil::str2endpoint(addr.c_str(), &endpt) < 0) {
      LOG(ERROR) << "Invalid sub mds ip:port provided: " << addr;
      return -1;
    }
  }

  inServiceIndex_ = 0;
  LOG(INFO) << "MDS address: " << options_.mdsListenAddr;

  copysetMan_ = options.copysetNodeManager;

  LOG(INFO) << "MDS timer: " << options_.intervalSec << " seconde";
  waitInterval_.Init(options_.intervalSec * 1000);

  startUpTime_ = utils::TimeUtility::GetTimeofDaySec();

  taskExecutor_ = std::make_unique<HeartbeatTaskExecutor>(copysetMan_, msEp_);

  return 0;
}

int Heartbeat::Run() {
  hbThread_ = utils::Thread(&Heartbeat::HeartbeatWorker, this);
  return 0;
}

int Heartbeat::Stop() {
  LOG(INFO) << "Stopping Heartbeat manager.";

  waitInterval_.StopWait();
  toStop_.store(true, std::memory_order_release);
  hbThread_.join();

  LOG(INFO) << "Stopped Heartbeat manager.";
  return 0;
}

int Heartbeat::Fini() {
  Stop();

  taskExecutor_.reset();

  LOG(INFO) << "Heartbeat manager cleaned up.";
  return 0;
}

void Heartbeat::BuildCopysetInfo(pb::mds::heartbeat::CopySetInfo* info,
                                 CopysetNode* copyset) {
  int ret;
  PoolId pool_id = copyset->GetPoolId();
  CopysetId copyset_id = copyset->GetCopysetId();

  info->set_poolid(pool_id);
  info->set_copysetid(copyset_id);
  info->set_epoch(copyset->GetConfEpoch());

  std::vector<Peer> peers;
  copyset->ListPeers(&peers);
  for (const Peer& peer : peers) {
    auto* replica = info->add_peers();
    replica->set_address(peer.address().c_str());
  }

  braft::PeerId leader = copyset->GetLeaderId();
  Peer* replica = new Peer();
  replica->set_address(leader.to_string());
  info->set_allocated_leaderpeer(replica);

  bool is_loading = copyset->IsLoading();
  info->set_iscopysetloading(is_loading);

  // add partition info
  if (is_loading) {
    LOG(WARNING) << "build copyset info for heartbeat get partition "
                 << "list fail, because copyset is loading, poolId = "
                 << pool_id << ", copysetId = " << copyset_id;
  } else {
    std::list<pb::common::PartitionInfo> partition_info_list;
    bool ret = copyset->GetPartitionInfoList(&partition_info_list);
    if (ret) {
      for (auto& it : partition_info_list) {
        info->add_partitioninfolist()->CopyFrom(it);
      }
    } else {
      LOG(WARNING) << "build copyset info for heartbeat get partition "
                   << "list fail, because copyset is loading, poolId = "
                   << pool_id << ", copysetId = " << copyset_id;
      info->set_iscopysetloading(true);
    }
  }

  ConfigChangeInfo conf_change_info;
  ret = GatherCopysetConfChange(copyset, &conf_change_info);
  if (ret == 0) {
    *info->mutable_configchangeinfo() = std::move(conf_change_info);
  }
}

// TODO(@Wine93): now we use memory storage, so we gather disk usage bytes
// which only has raft related capacity. If we use rocksdb storage, maybe
// we should need more flexible strategy.
bool Heartbeat::GetMetaserverSpaceStatus(MetaServerSpaceStatus* status,
                                         uint64_t ncopysets) {
  StorageStatistics statistics;
  bool succ = options_.resourceCollector->GetResourceStatistic(&statistics);
  if (!succ) {
    LOG(ERROR) << "Failed to get resource statistic";
    return false;
  }

  status->set_memorythresholdbyte(statistics.maxMemoryQuotaBytes);
  status->set_memoryusedbyte(statistics.memoryUsageBytes);
  status->set_diskthresholdbyte(statistics.maxDiskQuotaBytes);
  status->set_diskusedbyte(statistics.diskUsageBytes);
  if (ncopysets == 0) {
    status->set_memorycopysetminrequirebyte(0);
    status->set_diskcopysetminrequirebyte(0);
  } else {
    // TODO(all): report each copyset's resource usage
    status->set_memorycopysetminrequirebyte(
        uint64_t(statistics.memoryUsageBytes / ncopysets));
    status->set_diskcopysetminrequirebyte(
        uint64_t(statistics.diskUsageBytes / ncopysets));
  }

  return true;
}

int Heartbeat::BuildRequest(HeartbeatRequest* req) {
  req->set_metaserverid(options_.metaserverId);
  req->set_token(options_.metaserverToken);
  req->set_starttime(startUpTime_);
  req->set_ip(options_.ip);
  req->set_port(options_.port);

  std::vector<CopysetNode*> copysets;
  copysetMan_->GetAllCopysets(&copysets);

  req->set_copysetcount(copysets.size());
  int leaders = 0;

  for (auto* copyset : copysets) {
    pb::mds::heartbeat::CopySetInfo* info = req->add_copysetinfos();

    BuildCopysetInfo(info, copyset);

    if (copyset->IsLeaderTerm()) {
      ++leaders;
    }
  }
  req->set_leadercount(leaders);

  MetaServerSpaceStatus* status = req->mutable_spacestatus();
  if (!GetMetaserverSpaceStatus(status, copysets.size())) {
    LOG(ERROR) << "Get metaserver space status failed.";
    return -1;
  }

  return 0;
}

void Heartbeat::DumpHeartbeatRequest(const HeartbeatRequest& request) {
  VLOG(6) << "Heartbeat request: Metaserver ID: " << request.metaserverid()
          << ", IP = " << request.ip() << ", port = " << request.port()
          << ", copyset count = " << request.copysetcount()
          << ", leader count = " << request.leadercount()
          << ", diskThresholdByte = "
          << request.spacestatus().diskthresholdbyte()
          << ", diskCopysetMinRequireByte = "
          << request.spacestatus().diskcopysetminrequirebyte()
          << ", diskUsedByte = " << request.spacestatus().diskusedbyte()
          << ", memoryThresholdByte = "
          << request.spacestatus().memorythresholdbyte()
          << ", memoryCopySetMinRequireByte = "
          << request.spacestatus().memorycopysetminrequirebyte()
          << ", memoryUsedByte = " << request.spacestatus().memoryusedbyte();

  for (int i = 0; i < request.copysetinfos_size(); i++) {
    const pb::mds::heartbeat::CopySetInfo& info = request.copysetinfos(i);

    std::string peers_str;
    for (int j = 0; j < info.peers_size(); j++) {
      peers_str += info.peers(j).address() + ",";
    }

    VLOG(6) << "Copyset " << i << " "
            << copyset::ToGroupIdString(info.poolid(), info.copysetid())
            << ", epoch: " << info.epoch()
            << ", leader: " << info.leaderpeer().address()
            << ", peers: " << peers_str;
  }
}

void Heartbeat::DumpHeartbeatResponse(const HeartbeatResponse& response) {
  VLOG(3) << "Received heartbeat response, statusCode = "
          << response.statuscode();

  for (const auto& conf : response.needupdatecopysets()) {
    VLOG(3) << "need update copyset: " << conf.ShortDebugString();
  }
}

int Heartbeat::SendHeartbeat(const HeartbeatRequest& request,
                             HeartbeatResponse* response) {
  brpc::Channel channel;
  if (channel.Init(mdsEps_[inServiceIndex_].c_str(), nullptr) != 0) {
    LOG(ERROR) << msEp_.ip << ":" << msEp_.port
               << " Fail to init channel to MDS " << mdsEps_[inServiceIndex_];
    return -1;
  }

  pb::mds::heartbeat::HeartbeatService_Stub stub(&channel);
  brpc::Controller cntl;
  cntl.set_timeout_ms(options_.timeout);

  DumpHeartbeatRequest(request);

  LOG(INFO) << "Send heartbeat from metaserver: " << msEp_.ip << ":"
            << msEp_.port << " to mds :" << mdsEps_[inServiceIndex_];
  stub.MetaServerHeartbeat(&cntl, &request, response, nullptr);
  if (cntl.Failed()) {
    if (cntl.ErrorCode() == EHOSTDOWN || cntl.ErrorCode() == ETIMEDOUT ||
        cntl.ErrorCode() == brpc::ELOGOFF ||
        cntl.ErrorCode() == brpc::ERPCTIMEDOUT) {
      LOG(WARNING) << "current mds: " << mdsEps_[inServiceIndex_]
                   << " is shutdown or going to quit";
      inServiceIndex_ = (inServiceIndex_ + 1) % mdsEps_.size();
      LOG(INFO) << "next heartbeat switch to " << mdsEps_[inServiceIndex_];
    } else {
      LOG(ERROR) << msEp_.ip << ":" << msEp_.port
                 << " Fail to send heartbeat to MDS "
                 << mdsEps_[inServiceIndex_] << ","
                 << " cntl errorCode: " << cntl.ErrorCode()
                 << " cntl error: " << cntl.ErrorText();
    }
    return -1;
  } else {
    DumpHeartbeatResponse(*response);
  }

  return 0;
}

void Heartbeat::HeartbeatWorker() {
  int ret;
  int error_interval_sec = 2;

  LOG(INFO) << "Starting Heartbeat worker thread.";

  // Handling abnormal situations such as conf equal to 0
  if (options_.intervalSec <= 4) {
    error_interval_sec = 2;
  } else {
    error_interval_sec = options_.intervalSec / 2;
  }

  while (!toStop_.load(std::memory_order_acquire)) {
    HeartbeatRequest req;
    HeartbeatResponse resp;

    VLOG(3) << "building heartbeat info";
    ret = BuildRequest(&req);
    if (ret != 0) {
      LOG(ERROR) << "Failed to build heartbeat request";
      ::sleep(error_interval_sec);
      continue;
    }

    VLOG(3) << "sending heartbeat info";
    ret = SendHeartbeat(req, &resp);
    if (ret != 0) {
      LOG(WARNING) << "Failed to send heartbeat to MDS";
      ::sleep(error_interval_sec);
      continue;
    }

    taskExecutor_->ExecTasks(resp);
    waitInterval_.WaitForNextExcution();
  }

  LOG(INFO) << "Heartbeat worker thread stopped.";
}

HeartbeatTaskExecutor::HeartbeatTaskExecutor(copyset::CopysetNodeManager* mgr,
                                             const butil::EndPoint& endpoint)
    : copysetMgr_(mgr), ep_(endpoint) {}

void HeartbeatTaskExecutor::ExecTasks(const HeartbeatResponse& response) {
  for (const auto& conf : response.needupdatecopysets()) {
    ExecOneTask(conf);
  }
}

void HeartbeatTaskExecutor::ExecOneTask(const CopySetConf& conf) {
  auto* copyset = copysetMgr_->GetCopysetNode(conf.poolid(), conf.copysetid());
  if (!copyset) {
    LOG(WARNING) << "Failed to find copyset: " << CopysetName(conf);
    return;
  }

  if (NeedPurge(conf)) {
    DoPurgeCopyset(conf.poolid(), conf.copysetid());
    return;
  }

  const auto epoch_in_copyset = copyset->GetConfEpoch();
  if (conf.epoch() != epoch_in_copyset) {
    LOG(WARNING) << "Config change epoch: " << conf.epoch()
                 << " isn't same as current: " << epoch_in_copyset
                 << ", copyset: " << copyset->Name()
                 << ", refuse config change";
    return;
  }

  if (!conf.has_type()) {
    return;
  }

  switch (conf.type()) {
    case ConfigChangeType::TRANSFER_LEADER:
      DoTransferLeader(copyset, conf);
      break;
    case ConfigChangeType::ADD_PEER:
      DoAddPeer(copyset, conf);
      break;
    case ConfigChangeType::REMOVE_PEER:
      DoRemovePeer(copyset, conf);
      break;
    case ConfigChangeType::CHANGE_PEER:
      DoChangePeer(copyset, conf);
      break;
    default:
      LOG(ERROR) << "unexpected, type: " << conf.type();
      break;
  }
}

void HeartbeatTaskExecutor::DoTransferLeader(CopysetNode* node,
                                             const CopySetConf& conf) {
  LOG(INFO) << "Transferring leader to " << conf.configchangeitem().address()
            << " of copyset: " << node->Name();

  auto st = node->TransferLeader(conf.configchangeitem());
  if (!st.ok()) {
    LOG(WARNING) << "Transfer leader to " << conf.configchangeitem().address()
                 << " of copyset: " << node->Name()
                 << " failed, error: " << st.error_str();
  }
}

void HeartbeatTaskExecutor::DoAddPeer(CopysetNode* node,
                                      const CopySetConf& conf) {
  LOG(INFO) << "Adding peer " << conf.configchangeitem().address()
            << " to copyset: " << node->Name();
  node->AddPeer(conf.configchangeitem());
}

void HeartbeatTaskExecutor::DoRemovePeer(CopysetNode* node,
                                         const CopySetConf& conf) {
  LOG(INFO) << "Removing peer " << conf.configchangeitem().address()
            << " from copyset: " << node->Name();
  node->RemovePeer(conf.configchangeitem());
}

void HeartbeatTaskExecutor::DoChangePeer(CopysetNode* node,
                                         const CopySetConf& conf) {
  LOG(INFO) << "Change peer of copyset: " << node->Name()
            << ", adding: " << conf.configchangeitem().address()
            << ", removing: " << conf.oldpeer().address();

  std::vector<Peer> newpeers;
  for (const auto& p : conf.peers()) {
    if (p.address() != conf.oldpeer().address()) {
      newpeers.emplace_back(p);
    }
  }

  newpeers.emplace_back(conf.configchangeitem());
  node->ChangePeers(newpeers);
}

void HeartbeatTaskExecutor::DoPurgeCopyset(PoolId poolid, CopysetId copysetid) {
  bool rc = copysetMgr_->PurgeCopysetNode(poolid, copysetid);
  if (rc) {
    LOG(INFO) << "Purge copyset: " << ToGroupIdString(poolid, copysetid)
              << " success";
  } else {
    LOG(WARNING) << "Purge copyset: " << ToGroupIdString(poolid, copysetid)
                 << " failure";
  }
}

bool HeartbeatTaskExecutor::NeedPurge(const CopySetConf& conf) {
  Peer peer = EndPointToPeer(ep_);

  if (conf.epoch() == 0 && conf.peers().empty()) {
    LOG(INFO) << "Clean " << peer.address()
              << " from copyset: " << CopysetName(conf)
              << ", because it doesn't exist in mds record";
    return true;
  }

  auto iter = std::find_if(
      conf.peers().begin(), conf.peers().end(),
      [&peer](const Peer& p) { return peer.address() == p.address(); });

  if (iter == conf.peers().end()) {
    LOG(INFO) << "Clean " << peer.address()
              << " from copyset: " << CopysetName(conf)
              << ", because it doesn't exist in mds record";
    return true;
  }

  return false;
}

}  // namespace metaserver
}  // namespace dingofs
