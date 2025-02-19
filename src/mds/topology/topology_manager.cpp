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
 * Created Date: 2021-08-26
 * Author: wanghai01
 */

#include "mds/topology/topology_manager.h"

#include <sys/time.h>
#include <sys/types.h>

#include <algorithm>
#include <list>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "dingofs/topology.pb.h"
#include "mds/common/mds_define.h"
#include "mds/topology/deal_peerid.h"
#include "mds/topology/topology_item.h"
#include "utils/concurrent/name_lock.h"
#include "utils/timeutility.h"

namespace dingofs {
namespace mds {
namespace topology {

using dingofs::utils::NameLockGuard;
using dingofs::utils::TimeUtility;

using pb::mds::topology::PoolInfo;
using pb::mds::topology::ZoneInfo;

void TopologyManager::Init(const TopologyOption& option) { option_ = option; }

void TopologyManager::RegistMetaServer(
    const pb::mds::topology::MetaServerRegistRequest* request,
    pb::mds::topology::MetaServerRegistResponse* response) {
  std::string host_ip = request->internalip();
  uint32_t port = request->internalport();
  NameLockGuard lock(registMsMutex_, host_ip + ":" + std::to_string(port));

  // here we get metaserver already registered in the cluster that have
  // the same ip and port as what we're trying to register and are running
  // normally
  std::vector<MetaServerIdType> list = topology_->GetMetaServerInCluster(
      [&host_ip, &port](const MetaServer& ms) {
        return (ms.GetInternalIp() == host_ip) &&
               (ms.GetInternalPort() == port) &&
               (ms.GetOnlineState() != pb::mds::topology::OnlineState::OFFLINE);
      });
  if (1 == list.size()) {
    // report duplicated register (already a metaserver with same ip and
    // port in the cluster) to promise the idempotence of the interface.
    // If metaserver has copyset, return TOPO_METASERVER_EXIST;
    // else return OK
    auto copyset_list = topology_->GetCopySetsInMetaServer(list[0]);
    if (copyset_list.empty()) {
      MetaServer ms;
      topology_->GetMetaServer(list[0], &ms);
      response->set_statuscode(TopoStatusCode::TOPO_OK);
      response->set_metaserverid(ms.GetId());
      response->set_token(ms.GetToken());
      LOG(WARNING) << "Received duplicated registMetaServer message, "
                   << "metaserver is empty, hostip = " << host_ip
                   << ", port = " << port;
    } else {
      response->set_statuscode(TopoStatusCode::TOPO_METASERVER_EXIST);
      LOG(ERROR) << "Received duplicated registMetaServer message, "
                 << "metaserver is not empty, hostip = " << host_ip
                 << ", port = " << port;
    }

    return;
  } else if (list.size() > 1) {
    // more than one metaserver with same ip:port found, internal error
    response->set_statuscode(TopoStatusCode::TOPO_INTERNAL_ERROR);
    LOG(ERROR) << "Topology has counter an internal error: "
                  "Found metaServer data ipPort duplicated.";
    return;
  }

  ServerIdType server_id = topology_->FindServerByHostIpPort(
      request->internalip(), request->internalport());
  if (server_id == static_cast<ServerIdType>(UNINITIALIZE_ID)) {
    response->set_statuscode(TopoStatusCode::TOPO_SERVER_NOT_FOUND);
    return;
  }

  MetaServerIdType meta_server_id = topology_->AllocateMetaServerId();
  if (meta_server_id == static_cast<MetaServerIdType>(UNINITIALIZE_ID)) {
    response->set_statuscode(TopoStatusCode::TOPO_ALLOCATE_ID_FAIL);
    return;
  }

  std::string token = topology_->AllocateToken();
  Server server;
  bool found_server = topology_->GetServer(server_id, &server);
  if (!found_server) {
    LOG(ERROR) << "Get server " << server_id << " from topology fail";
    response->set_statuscode(TopoStatusCode::TOPO_SERVER_NOT_FOUND);
    return;
  }
  if (request->has_externalip()) {
    if (request->externalip() != server.GetExternalIp()) {
      LOG(ERROR) << "External ip of metaserver not match server's"
                 << ", server external ip: " << server.GetExternalIp()
                 << ", request external ip: " << request->externalip();
      response->set_statuscode(TopoStatusCode::TOPO_INTERNAL_ERROR);
      return;
    }
  }

  MetaServer metaserver(meta_server_id, request->hostname(), token, server_id,
                        request->internalip(), request->internalport(),
                        request->externalip(), request->externalport(),
                        pb::mds::topology::OnlineState::ONLINE);

  TopoStatusCode errcode = topology_->AddMetaServer(metaserver);
  if (errcode == TopoStatusCode::TOPO_OK) {
    response->set_statuscode(TopoStatusCode::TOPO_OK);
    response->set_metaserverid(metaserver.GetId());
    response->set_token(metaserver.GetToken());
  } else {
    response->set_statuscode(errcode);
  }
}

void TopologyManager::ListMetaServer(
    const pb::mds::topology::ListMetaServerRequest* request,
    pb::mds::topology::ListMetaServerResponse* response) {
  Server server;
  if (!topology_->GetServer(request->serverid(), &server)) {
    response->set_statuscode(TopoStatusCode::TOPO_SERVER_NOT_FOUND);
    return;
  }

  std::list<MetaServerIdType> metaserver_list = server.GetMetaServerList();
  response->set_statuscode(TopoStatusCode::TOPO_OK);

  for (MetaServerIdType id : metaserver_list) {
    MetaServer ms;
    if (topology_->GetMetaServer(id, &ms)) {
      pb::mds::topology::MetaServerInfo* ms_info =
          response->add_metaserverinfos();
      ms_info->set_metaserverid(ms.GetId());
      ms_info->set_hostname(ms.GetHostName());
      ms_info->set_internalip(ms.GetInternalIp());
      ms_info->set_internalport(ms.GetInternalPort());
      ms_info->set_externalip(ms.GetExternalIp());
      ms_info->set_externalport(ms.GetExternalPort());
      ms_info->set_onlinestate(ms.GetOnlineState());
    } else {
      LOG(ERROR) << "Topology has counter an internal error: "
                 << "[func:] ListMetaServer, "
                 << "[msg:] metaserver not found, id = " << id;
      response->set_statuscode(TopoStatusCode::TOPO_INTERNAL_ERROR);
      return;
    }
  }
}

void TopologyManager::GetMetaServer(
    const pb::mds::topology::GetMetaServerInfoRequest* request,
    pb::mds::topology::GetMetaServerInfoResponse* response) {
  MetaServer ms;
  if (request->has_metaserverid()) {
    if (!topology_->GetMetaServer(request->metaserverid(), &ms)) {
      response->set_statuscode(TopoStatusCode::TOPO_METASERVER_NOT_FOUND);
      return;
    }
  } else if (request->has_hostip() && request->has_port()) {
    if (!topology_->GetMetaServer(request->hostip(), request->port(), &ms)) {
      response->set_statuscode(TopoStatusCode::TOPO_METASERVER_NOT_FOUND);
      return;
    }
  } else {
    response->set_statuscode(TopoStatusCode::TOPO_INVALID_PARAM);
    return;
  }
  response->set_statuscode(TopoStatusCode::TOPO_OK);
  pb::mds::topology::MetaServerInfo* ms_info =
      response->mutable_metaserverinfo();
  ms_info->set_metaserverid(ms.GetId());
  ms_info->set_hostname(ms.GetHostName());
  ms_info->set_internalip(ms.GetInternalIp());
  ms_info->set_internalport(ms.GetInternalPort());
  ms_info->set_externalip(ms.GetExternalIp());
  ms_info->set_externalport(ms.GetExternalPort());
  ms_info->set_onlinestate(ms.GetOnlineState());
}

void TopologyManager::DeleteMetaServer(
    const pb::mds::topology::DeleteMetaServerRequest* request,
    pb::mds::topology::DeleteMetaServerResponse* response) {
  TopoStatusCode errcode = topology_->RemoveMetaServer(request->metaserverid());
  response->set_statuscode(errcode);
}

void TopologyManager::RegistServer(
    const pb::mds::topology::ServerRegistRequest* request,
    pb::mds::topology::ServerRegistResponse* response) {
  Pool p_pool;
  if (!topology_->GetPool(request->poolname(), &p_pool)) {
    response->set_statuscode(TopoStatusCode::TOPO_POOL_NOT_FOUND);
    return;
  }

  Zone zone;
  if (!topology_->GetZone(request->zonename(), p_pool.GetId(), &zone)) {
    response->set_statuscode(TopoStatusCode::TOPO_ZONE_NOT_FOUND);
    return;
  }

  uint32_t internal_port = 0;
  if (request->has_internalport()) {
    internal_port = request->internalport();
  }
  uint32_t external_port = 0;
  if (request->has_externalport()) {
    external_port = request->externalport();
  }

  // check whether there's any duplicated ip&port
  if (topology_->FindServerByHostIpPort(request->internalip(), internal_port) !=
      static_cast<ServerIdType>(UNINITIALIZE_ID)) {
    response->set_statuscode(TopoStatusCode::TOPO_IP_PORT_DUPLICATED);
    return;
  } else if (topology_->FindServerByHostIpPort(request->externalip(),
                                               external_port) !=
             static_cast<ServerIdType>(UNINITIALIZE_ID)) {
    response->set_statuscode(TopoStatusCode::TOPO_IP_PORT_DUPLICATED);
    return;
  }

  ServerIdType server_id = topology_->AllocateServerId();
  if (server_id == static_cast<ServerIdType>(UNINITIALIZE_ID)) {
    response->set_statuscode(TopoStatusCode::TOPO_ALLOCATE_ID_FAIL);
    return;
  }

  Server server(server_id, request->hostname(), request->internalip(),
                internal_port, request->externalip(), external_port,
                zone.GetId(), p_pool.GetId());

  TopoStatusCode errcode = topology_->AddServer(server);
  if (TopoStatusCode::TOPO_OK == errcode) {
    response->set_statuscode(TopoStatusCode::TOPO_OK);
    response->set_serverid(server_id);
  } else {
    response->set_statuscode(errcode);
  }
}

void TopologyManager::GetServer(
    const pb::mds::topology::GetServerRequest* request,
    pb::mds::topology::GetServerResponse* response) {
  Server sv;
  if (request->has_serverid()) {
    if (!topology_->GetServer(request->serverid(), &sv)) {
      response->set_statuscode(TopoStatusCode::TOPO_SERVER_NOT_FOUND);
      return;
    }
  } else if (request->has_hostname()) {
    if (!topology_->GetServerByHostName(request->hostname(), &sv)) {
      response->set_statuscode(TopoStatusCode::TOPO_SERVER_NOT_FOUND);
      return;
    }
  } else if (request->has_hostip()) {
    uint32_t port = 0;
    if (request->has_port()) {
      port = request->port();
    }
    if (!topology_->GetServerByHostIpPort(request->hostip(), port, &sv)) {
      response->set_statuscode(TopoStatusCode::TOPO_SERVER_NOT_FOUND);
      return;
    }
  }
  Zone zone;
  if (!topology_->GetZone(sv.GetZoneId(), &zone)) {
    LOG(ERROR) << "Topology has counter an internal error: "
               << " Server belong Zone not found, ServerId = " << sv.GetId()
               << " ZoneId = " << sv.GetZoneId();
    response->set_statuscode(TopoStatusCode::TOPO_INTERNAL_ERROR);
    return;
  }
  Pool pPool;
  if (!topology_->GetPool(zone.GetPoolId(), &pPool)) {
    LOG(ERROR) << "Topology has counter an internal error: "
               << " Zone belong Pool not found, zoneId = " << zone.GetId()
               << " poolId = " << zone.GetPoolId();
    response->set_statuscode(TopoStatusCode::TOPO_INTERNAL_ERROR);
    return;
  }
  pb::mds::topology::ServerInfo* info = new pb::mds::topology::ServerInfo();
  info->set_serverid(sv.GetId());
  info->set_hostname(sv.GetHostName());
  info->set_internalip(sv.GetInternalIp());
  info->set_internalport(sv.GetInternalPort());
  info->set_externalip(sv.GetExternalIp());
  info->set_externalport(sv.GetExternalPort());
  info->set_zoneid(sv.GetZoneId());
  info->set_zonename(zone.GetName());
  info->set_poolid(sv.GetPoolId());
  info->set_poolname(pPool.GetName());
  response->set_allocated_serverinfo(info);
}

void TopologyManager::DeleteServer(
    const pb::mds::topology::DeleteServerRequest* request,
    pb::mds::topology::DeleteServerResponse* response) {
  TopoStatusCode errcode = TopoStatusCode::TOPO_OK;
  Server server;
  if (!topology_->GetServer(request->serverid(), &server)) {
    response->set_statuscode(TopoStatusCode::TOPO_SERVER_NOT_FOUND);
    return;
  }
  for (auto& msId : server.GetMetaServerList()) {
    MetaServer ms;
    if (!topology_->GetMetaServer(msId, &ms)) {
      LOG(ERROR) << "Topology has counter an internal error: "
                 << ", metaServer in server not found"
                 << ", metaserverId = " << msId
                 << ", serverId = " << request->serverid();
      response->set_statuscode(TopoStatusCode::TOPO_INTERNAL_ERROR);
      return;
    } else if (pb::mds::topology::OnlineState::OFFLINE != ms.GetOnlineState()) {
      LOG(ERROR) << "Can not delete server which have "
                 << "metaserver not offline.";
      response->set_statuscode(TopoStatusCode::TOPO_CANNOT_REMOVE_NOT_OFFLINE);
      return;
    } else {
      errcode = topology_->RemoveMetaServer(msId);
      if (errcode != TopoStatusCode::TOPO_OK) {
        response->set_statuscode(errcode);
        return;
      }
    }
  }
  errcode = topology_->RemoveServer(request->serverid());
  response->set_statuscode(errcode);
}

void TopologyManager::ListZoneServer(
    const pb::mds::topology::ListZoneServerRequest* request,
    pb::mds::topology::ListZoneServerResponse* response) {
  Zone zone;
  if (request->has_zoneid()) {
    if (!topology_->GetZone(request->zoneid(), &zone)) {
      response->set_statuscode(TopoStatusCode::TOPO_ZONE_NOT_FOUND);
      return;
    }
  } else if (request->has_zonename() && request->has_poolname()) {
    if (!topology_->GetZone(request->zonename(), request->poolname(), &zone)) {
      response->set_statuscode(TopoStatusCode::TOPO_ZONE_NOT_FOUND);
      return;
    }
  } else {
    response->set_statuscode(TopoStatusCode::TOPO_INVALID_PARAM);
    return;
  }
  response->set_statuscode(TopoStatusCode::TOPO_OK);
  std::list<ServerIdType> serverIdList = zone.GetServerList();
  for (ServerIdType id : serverIdList) {
    Server sv;
    if (topology_->GetServer(id, &sv)) {
      Zone zone;
      if (!topology_->GetZone(sv.GetZoneId(), &zone)) {
        LOG(ERROR) << "Topology has counter an internal error: "
                   << " Server belong Zone not found, ServerId = " << sv.GetId()
                   << " ZoneId = " << sv.GetZoneId();
        response->set_statuscode(TopoStatusCode::TOPO_INTERNAL_ERROR);
        return;
      }
      Pool pPool;
      if (!topology_->GetPool(zone.GetPoolId(), &pPool)) {
        LOG(ERROR) << "Topology has counter an internal error: "
                   << " Zone belong Pool not found, zoneId = " << zone.GetId()
                   << " poolId = " << zone.GetPoolId();
        response->set_statuscode(TopoStatusCode::TOPO_INTERNAL_ERROR);
        return;
      }
      pb::mds::topology::ServerInfo* info = response->add_serverinfo();
      info->set_serverid(sv.GetId());
      info->set_hostname(sv.GetHostName());
      info->set_internalip(sv.GetInternalIp());
      info->set_internalport(sv.GetInternalPort());
      info->set_externalip(sv.GetExternalIp());
      info->set_externalport(sv.GetExternalPort());
      info->set_zoneid(sv.GetZoneId());
      info->set_zonename(zone.GetName());
      info->set_poolid(sv.GetPoolId());
      info->set_poolname(pPool.GetName());
    } else {
      LOG(ERROR) << "Topology has counter an internal error: "
                 << "[func:] ListZoneServer, "
                 << "[msg:] server not found, id = " << id;
      response->set_statuscode(TopoStatusCode::TOPO_INTERNAL_ERROR);
      return;
    }
  }
}

void TopologyManager::CreateZone(
    const pb::mds::topology::CreateZoneRequest* request,
    pb::mds::topology::CreateZoneResponse* response) {
  Pool pPool;
  if (!topology_->GetPool(request->poolname(), &pPool)) {
    response->set_statuscode(TopoStatusCode::TOPO_POOL_NOT_FOUND);
    return;
  }
  if (topology_->FindZone(request->zonename(), pPool.GetId()) !=
      static_cast<PoolIdType>(UNINITIALIZE_ID)) {
    response->set_statuscode(TopoStatusCode::TOPO_NAME_DUPLICATED);
    return;
  }

  ZoneIdType zid = topology_->AllocateZoneId();
  if (zid == static_cast<ZoneIdType>(UNINITIALIZE_ID)) {
    response->set_statuscode(TopoStatusCode::TOPO_ALLOCATE_ID_FAIL);
    return;
  }
  Zone zone(zid, request->zonename(), pPool.GetId());
  TopoStatusCode errcode = topology_->AddZone(zone);
  if (TopoStatusCode::TOPO_OK == errcode) {
    response->set_statuscode(errcode);
    ZoneInfo* info = new ZoneInfo();
    info->set_zoneid(zid);
    info->set_zonename(request->zonename());
    info->set_poolid(pPool.GetId());
    info->set_poolname(pPool.GetName());
    response->set_allocated_zoneinfo(info);
  } else {
    response->set_statuscode(errcode);
  }
}

void TopologyManager::DeleteZone(
    const pb::mds::topology::DeleteZoneRequest* request,
    pb::mds::topology::DeleteZoneResponse* response) {
  Zone zone;
  if (!topology_->GetZone(request->zoneid(), &zone)) {
    response->set_statuscode(TopoStatusCode::TOPO_ZONE_NOT_FOUND);
    return;
  }
  TopoStatusCode errcode = topology_->RemoveZone(zone.GetId());
  response->set_statuscode(errcode);
}

void TopologyManager::GetZone(const pb::mds::topology::GetZoneRequest* request,
                              pb::mds::topology::GetZoneResponse* response) {
  Zone zone;
  if (!topology_->GetZone(request->zoneid(), &zone)) {
    response->set_statuscode(TopoStatusCode::TOPO_ZONE_NOT_FOUND);
    return;
  }
  Pool pPool;
  if (!topology_->GetPool(zone.GetPoolId(), &pPool)) {
    response->set_statuscode(TopoStatusCode::TOPO_INTERNAL_ERROR);
    return;
  }
  response->set_statuscode(TopoStatusCode::TOPO_OK);
  ZoneInfo* info = new ZoneInfo();
  info->set_zoneid(zone.GetId());
  info->set_zonename(zone.GetName());
  info->set_poolid((zone.GetPoolId()));
  info->set_poolname(pPool.GetName());
  response->set_allocated_zoneinfo(info);
}

void TopologyManager::ListPoolZone(
    const pb::mds::topology::ListPoolZoneRequest* request,
    pb::mds::topology::ListPoolZoneResponse* response) {
  Pool pPool;
  if (!topology_->GetPool(request->poolid(), &pPool)) {
    response->set_statuscode(TopoStatusCode::TOPO_POOL_NOT_FOUND);
    return;
  }
  std::list<ZoneIdType> zidList = pPool.GetZoneList();
  response->set_statuscode(TopoStatusCode::TOPO_OK);
  for (ZoneIdType id : zidList) {
    Zone zone;
    if (topology_->GetZone(id, &zone)) {
      ZoneInfo* info = response->add_zones();
      info->set_zoneid(zone.GetId());
      info->set_zonename(zone.GetName());
      info->set_poolid(pPool.GetId());
      info->set_poolname(pPool.GetName());
    } else {
      LOG(ERROR) << "Topology has counter an internal error: "
                 << "[func:] ListPoolZone, "
                 << "[msg:] Zone not found, id = " << id;
      response->set_statuscode(TopoStatusCode::TOPO_INTERNAL_ERROR);
      return;
    }
  }
}

void TopologyManager::CreatePool(
    const pb::mds::topology::CreatePoolRequest* request,
    pb::mds::topology::CreatePoolResponse* response) {
  if (topology_->FindPool(request->poolname()) !=
      static_cast<PoolIdType>(UNINITIALIZE_ID)) {
    response->set_statuscode(TopoStatusCode::TOPO_NAME_DUPLICATED);
    return;
  }

  PoolIdType pid = topology_->AllocatePoolId();
  if (pid == static_cast<PoolIdType>(UNINITIALIZE_ID)) {
    response->set_statuscode(TopoStatusCode::TOPO_ALLOCATE_ID_FAIL);
    return;
  }

  Pool::RedundanceAndPlaceMentPolicy rap;
  if (!Pool::TransRedundanceAndPlaceMentPolicyFromJsonStr(
          request->redundanceandplacementpolicy(), &rap)) {
    LOG(ERROR) << "[TopologyManager::CreatePool]:"
               << "parse redundanceandplacementpolicy fail.";
    response->set_statuscode(TopoStatusCode::TOPO_INVALID_PARAM);
    return;
  }

  uint64_t time = TimeUtility::GetTimeofDaySec();

  Pool pool(pid, request->poolname(), rap, time);

  TopoStatusCode errcode = topology_->AddPool(pool);
  if (TopoStatusCode::TOPO_OK == errcode) {
    response->set_statuscode(errcode);
    PoolInfo* info = new PoolInfo();
    info->set_poolid(pid);
    info->set_poolname(request->poolname());
    info->set_createtime(time);
    info->set_redundanceandplacementpolicy(
        pool.GetRedundanceAndPlaceMentPolicyJsonStr());
    response->set_allocated_poolinfo(info);
  } else {
    response->set_statuscode(errcode);
  }
}

void TopologyManager::DeletePool(
    const pb::mds::topology::DeletePoolRequest* request,
    pb::mds::topology::DeletePoolResponse* response) {
  Pool pool;
  if (!topology_->GetPool(request->poolid(), &pool)) {
    response->set_statuscode(TopoStatusCode::TOPO_POOL_NOT_FOUND);
    return;
  }

  TopoStatusCode errcode = topology_->RemovePool(pool.GetId());
  response->set_statuscode(errcode);
}

void TopologyManager::GetPool(const pb::mds::topology::GetPoolRequest* request,
                              pb::mds::topology::GetPoolResponse* response) {
  Pool pool;
  if (!topology_->GetPool(request->poolid(), &pool)) {
    response->set_statuscode(TopoStatusCode::TOPO_POOL_NOT_FOUND);
    return;
  }

  response->set_statuscode(TopoStatusCode::TOPO_OK);
  PoolInfo* info = new PoolInfo();
  info->set_poolid(pool.GetId());
  info->set_poolname(pool.GetName());
  info->set_createtime(pool.GetCreateTime());
  info->set_redundanceandplacementpolicy(
      pool.GetRedundanceAndPlaceMentPolicyJsonStr());
  response->set_allocated_poolinfo(info);
}

void TopologyManager::ListPool(
    const pb::mds::topology::ListPoolRequest* request,
    pb::mds::topology::ListPoolResponse* response) {
  (void)request;
  response->set_statuscode(TopoStatusCode::TOPO_OK);
  auto poolList = topology_->GetPoolInCluster();
  for (PoolIdType id : poolList) {
    Pool pool;
    if (topology_->GetPool(id, &pool)) {
      PoolInfo* info = response->add_poolinfos();
      info->set_poolid(pool.GetId());
      info->set_poolname(pool.GetName());
      info->set_createtime(pool.GetCreateTime());
      info->set_redundanceandplacementpolicy(
          pool.GetRedundanceAndPlaceMentPolicyJsonStr());
    } else {
      LOG(ERROR) << "Topology has counter an internal error: "
                 << "[func:] ListPool, "
                 << "[msg:] Pool not found, id = " << id;
      response->set_statuscode(TopoStatusCode::TOPO_INTERNAL_ERROR);
      return;
    }
  }
}

TopoStatusCode TopologyManager::CreatePartitionsAndGetMinPartition(
    FsIdType fsId, pb::common::PartitionInfo* partition) {
  pb::mds::topology::CreatePartitionRequest request;
  pb::mds::topology::CreatePartitionResponse response;
  request.set_fsid(fsId);
  request.set_count(option_.createPartitionNumber);
  CreatePartitions(&request, &response);
  if (TopoStatusCode::TOPO_OK != response.statuscode() ||
      response.partitioninfolist_size() != static_cast<int>(request.count())) {
    return TopoStatusCode::TOPO_CREATE_PARTITION_FAIL;
  }
  // return the min one
  PartitionIdType minId = 0;
  if (response.partitioninfolist_size() > 0) {
    minId = response.partitioninfolist(0).partitionid();
    *partition = response.partitioninfolist(0);
    for (int i = 1; i < response.partitioninfolist_size(); i++) {
      if (response.partitioninfolist(i).partitionid() < minId) {
        minId = response.partitioninfolist(i).partitionid();
        *partition = response.partitioninfolist(i);
      }
    }
  } else {
    LOG(WARNING) << "CreatePartition but empty response, "
                 << "request = " << request.ShortDebugString()
                 << "response = " << response.ShortDebugString();
    return TopoStatusCode::TOPO_CREATE_PARTITION_FAIL;
  }
  return TopoStatusCode::TOPO_OK;
}

TopoStatusCode TopologyManager::CreatePartitionOnCopyset(
    FsIdType fsId, const CopySetInfo& copyset,
    pb::common::PartitionInfo* info) {
  // get copyset members
  std::set<MetaServerIdType> copysetMembers = copyset.GetCopySetMembers();
  std::set<std::string> copysetMemberAddr;
  for (auto item : copysetMembers) {
    MetaServer metaserver;
    if (topology_->GetMetaServer(item, &metaserver)) {
      std::string addr = metaserver.GetInternalIp() + ":" +
                         std::to_string(metaserver.GetInternalPort());
      copysetMemberAddr.emplace(addr);
    } else {
      LOG(WARNING) << "Get metaserver info failed.";
    }
  }

  // calculate inodeId start and end of partition
  uint32_t index = topology_->GetPartitionIndexOfFS(fsId);
  uint64_t idStart = index * option_.idNumberInPartition;
  uint64_t idEnd = (index + 1) * option_.idNumberInPartition - 1;
  PartitionIdType partitionId = topology_->AllocatePartitionId();

  if (partitionId == static_cast<ServerIdType>(UNINITIALIZE_ID)) {
    return TopoStatusCode::TOPO_ALLOCATE_ID_FAIL;
  }

  PoolIdType poolId = copyset.GetPoolId();
  CopySetIdType copysetId = copyset.GetId();
  LOG(INFO) << "CreatePartiton for fs: " << fsId << ", on copyset:(" << poolId
            << ", " << copysetId << "), partitionId = " << partitionId
            << ", start = " << idStart << ", end = " << idEnd;

  FSStatusCode retcode = metaserverClient_->CreatePartition(
      fsId, poolId, copysetId, partitionId, idStart, idEnd, copysetMemberAddr);
  if (FSStatusCode::OK != retcode) {
    LOG(ERROR) << "CreatePartition failed, " << "fsId = " << fsId
               << ", poolId = " << poolId << ", copysetId = " << copysetId
               << ", partitionId = " << partitionId;
    return TopoStatusCode::TOPO_CREATE_PARTITION_FAIL;
  }

  Partition partition(fsId, poolId, copysetId, partitionId, idStart, idEnd);
  TopoStatusCode ret = topology_->AddPartition(partition);
  if (TopoStatusCode::TOPO_OK != ret) {
    // TODO(wanghai): delete partition on metaserver
    LOG(ERROR) << "Add partition failed after create partition."
               << " error code = " << ret;
    return ret;
  }

  info->set_fsid(fsId);
  info->set_poolid(poolId);
  info->set_copysetid(copysetId);
  info->set_partitionid(partitionId);
  info->set_start(idStart);
  info->set_end(idEnd);
  info->set_txid(0);
  info->set_status(pb::common::PartitionStatus::READWRITE);

  return TopoStatusCode::TOPO_OK;
}

void TopologyManager::CreatePartitions(
    const pb::mds::topology::CreatePartitionRequest* request,
    pb::mds::topology::CreatePartitionResponse* response) {
  FsIdType fsId = request->fsid();
  uint32_t count = request->count();
  auto partitionInfoList = response->mutable_partitioninfolist();
  response->set_statuscode(TopoStatusCode::TOPO_OK);

  // get lock and avoid multiMountpoint create concurrently
  NameLockGuard lock(createPartitionMutex_, std::to_string(fsId));

  while (partitionInfoList->size() < static_cast<int>(count)) {
    int32_t createNum = count - topology_->GetAvailableCopysetNum();
    // if available copyset is not enough, create copyset first
    if (createNum > 0) {
      if (CreateEnoughCopyset(createNum) != TopoStatusCode::TOPO_OK) {
        LOG(ERROR) << "Create copyset failed when create partition.";
        response->set_statuscode(TopoStatusCode::TOPO_CREATE_COPYSET_ERROR);
        return;
      }
    }

    std::vector<CopySetInfo> copysetVec = topology_->GetAvailableCopysetList();
    if (copysetVec.size() == 0) {
      LOG(ERROR) << "Get available copyset fail when create partition.";
      response->set_statuscode(
          TopoStatusCode::TOPO_GET_AVAILABLE_COPYSET_ERROR);
      return;
    }

    // sort copysetVec by partition num desent
    std::sort(copysetVec.begin(), copysetVec.end(),
              [](const CopySetInfo& a, const CopySetInfo& b) {
                return a.GetPartitionNum() < b.GetPartitionNum();
              });

    uint32_t copysetNum = copysetVec.size();
    int32_t tempCount = std::min(copysetNum, count - partitionInfoList->size());

    for (int i = 0; i < tempCount; i++) {
      pb::common::PartitionInfo* info = partitionInfoList->Add();
      TopoStatusCode ret = CreatePartitionOnCopyset(fsId, copysetVec[i], info);
      if (ret != TopoStatusCode::TOPO_OK) {
        LOG(ERROR) << "create partition on copyset fail, fsId = " << fsId
                   << ", poolId = " << copysetVec[i].GetPoolId()
                   << ", copysetId = " << copysetVec[i].GetId();
        response->set_statuscode(ret);
        return;
      }
    }
  }
}

TopoStatusCode TopologyManager::DeletePartition(uint32_t partition_id) {
  pb::mds::topology::DeletePartitionRequest request;
  pb::mds::topology::DeletePartitionResponse response;
  request.set_partitionid(partition_id);
  DeletePartition(&request, &response);

  if (TopoStatusCode::TOPO_OK != response.statuscode()) {
    return TopoStatusCode::TOPO_DELETE_PARTITION_ON_METASERVER_FAIL;
  }

  return TopoStatusCode::TOPO_OK;
}

void TopologyManager::DeletePartition(
    const pb::mds::topology::DeletePartitionRequest* request,
    pb::mds::topology::DeletePartitionResponse* response) {
  uint32_t partitionId = request->partitionid();
  Partition partition;
  if (!topology_->GetPartition(partitionId, &partition)) {
    LOG(WARNING) << "Get Partiton info failed, id = " << partitionId;
    response->set_statuscode(TopoStatusCode::TOPO_OK);
    return;
  }

  if (partition.GetStatus() == pb::common::PartitionStatus::DELETING) {
    LOG(WARNING) << "Delete partition which is deleting already, id =  "
                 << partitionId;
    response->set_statuscode(TopoStatusCode::TOPO_OK);
    return;
  }

  uint32_t poolId = partition.GetPoolId();
  uint32_t copysetId = partition.GetCopySetId();

  // get copyset members
  std::set<std::string> copysetMemberAddr;
  auto ret = GetCopysetMembers(poolId, copysetId, &copysetMemberAddr);
  if (ret != TopoStatusCode::TOPO_OK) {
    LOG(ERROR) << "GetCopysetMembers failed, poolId = " << poolId
               << ", copysetId = " << copysetId;
    response->set_statuscode(ret);
    return;
  }

  auto fret = metaserverClient_->DeletePartition(poolId, copysetId, partitionId,
                                                 copysetMemberAddr);
  if (fret == FSStatusCode::OK || fret == FSStatusCode::UNDER_DELETING) {
    ret = topology_->UpdatePartitionStatus(
        partitionId, pb::common::PartitionStatus::DELETING);
    if (ret != TopoStatusCode::TOPO_OK) {
      LOG(ERROR) << "DeletePartition failed, partitionId = " << partitionId
                 << ", ret = " << TopoStatusCode_Name(ret);
    }
    response->set_statuscode(ret);
    return;
  }
  response->set_statuscode(
      TopoStatusCode::TOPO_DELETE_PARTITION_ON_METASERVER_FAIL);
}

bool TopologyManager::CreateCopysetNodeOnMetaServer(
    PoolIdType poolId, CopySetIdType copysetId, MetaServerIdType metaServerId) {
  MetaServer metaserver;
  std::string addr;
  if (topology_->GetMetaServer(metaServerId, &metaserver)) {
    addr = metaserver.GetInternalIp() + ":" +
           std::to_string(metaserver.GetInternalPort());
  } else {
    LOG(ERROR) << "Get metaserver info failed.";
    return false;
  }

  FSStatusCode retcode =
      metaserverClient_->CreateCopySetOnOneMetaserver(poolId, copysetId, addr);
  if (FSStatusCode::OK != retcode) {
    LOG(ERROR) << "CreateCopysetNodeOnMetaServer fail, poolId = " << poolId
               << ", copysetId = " << copysetId
               << ", metaServerId = " << metaServerId << ", addr = " << addr
               << ", ret = " << FSStatusCode_Name(retcode);
    return false;
  }
  return true;
}

void TopologyManager::ClearCopysetCreating(PoolIdType poolId,
                                           CopySetIdType copysetId) {
  topology_->RemoveCopySetCreating(CopySetKey(poolId, copysetId));
}

TopoStatusCode TopologyManager::CreateEnoughCopyset(int32_t createNum) {
  std::list<CopysetCreateInfo> copysetList;
  // gen copyset add, the copyset num >= createNum
  auto ret = topology_->GenCopysetAddrBatch(createNum, &copysetList);
  if (ret != TopoStatusCode::TOPO_OK) {
    LOG(ERROR) << "create copyset generate copyset addr fail, createNum = "
               << createNum;
    return ret;
  }

  for (auto copyset : copysetList) {
    // alloce copyset id
    auto copysetId = topology_->AllocateCopySetId(copyset.poolId);
    if (copysetId == static_cast<ServerIdType>(UNINITIALIZE_ID)) {
      return TopoStatusCode::TOPO_ALLOCATE_ID_FAIL;
    }

    copyset.copysetId = copysetId;
    ret = CreateCopyset(copyset);
    if (ret != TopoStatusCode::TOPO_OK) {
      LOG(ERROR) << "initial create copyset, create copyset fail";
      return ret;
    }
  }

  return TopoStatusCode::TOPO_OK;
}

TopoStatusCode TopologyManager::CreateCopyset(
    const CopysetCreateInfo& copyset) {
  LOG(INFO) << "Create new copyset: " << copyset.ToString();
  // translate metaserver id to metaserver addr
  std::set<std::string> metaServerAddrs;
  for (const auto& it : copyset.metaServerIds) {
    MetaServer metaServer;
    if (topology_->GetMetaServer(it, &metaServer)) {
      metaServerAddrs.emplace(metaServer.GetInternalIp() + ":" +
                              std::to_string(metaServer.GetInternalPort()));
    } else {
      LOG(ERROR) << "get metaserver failed, metaserverId = " << it;
      return TopoStatusCode::TOPO_METASERVER_NOT_FOUND;
    }
  }

  if (TopoStatusCode::TOPO_OK != topology_->AddCopySetCreating(CopySetKey(
                                     copyset.poolId, copyset.copysetId))) {
    LOG(WARNING) << "the copyset key = (" << copyset.poolId << ", "
                 << copyset.copysetId << ") is already creating.";
  }

  // create copyset on metaserver
  FSStatusCode retcode = metaserverClient_->CreateCopySet(
      copyset.poolId, copyset.copysetId, metaServerAddrs);
  if (FSStatusCode::OK != retcode) {
    ClearCopysetCreating(copyset.poolId, copyset.copysetId);
    return TopoStatusCode::TOPO_CREATE_COPYSET_ON_METASERVER_FAIL;
  }

  // add copyset record to topology
  CopySetInfo copysetInfo(copyset.poolId, copyset.copysetId);
  copysetInfo.SetCopySetMembers(copyset.metaServerIds);
  auto ret = topology_->AddCopySet(copysetInfo);
  if (TopoStatusCode::TOPO_OK != ret) {
    LOG(ERROR) << "Add copyset failed after create copyset."
               << " poolId = " << copyset.poolId
               << ", copysetId = " << copyset.copysetId
               << ", error msg = " << TopoStatusCode_Name(ret);
    ClearCopysetCreating(copyset.poolId, copyset.copysetId);
    return ret;
  }

  ClearCopysetCreating(copyset.poolId, copyset.copysetId);
  return TopoStatusCode::TOPO_OK;
}

TopoStatusCode TopologyManager::CommitTxId(
    const std::vector<pb::mds::topology::PartitionTxId>& txIds) {
  if (txIds.size() == 0) {
    return TopoStatusCode::TOPO_OK;
  }
  return topology_->UpdatePartitionTxIds(txIds);
}

void TopologyManager::CommitTx(
    const pb::mds::topology::CommitTxRequest* request,
    pb::mds::topology::CommitTxResponse* response) {
  std::vector<pb::mds::topology::PartitionTxId> txIds;
  for (int i = 0; i < request->partitiontxids_size(); i++) {
    txIds.emplace_back(request->partitiontxids(i));
  }
  TopoStatusCode rc = CommitTxId(txIds);
  response->set_statuscode(rc);
}

void TopologyManager::GetMetaServerListInCopysets(
    const pb::mds::topology::GetMetaServerListInCopySetsRequest* request,
    pb::mds::topology::GetMetaServerListInCopySetsResponse* response) {
  PoolIdType poolId = request->poolid();
  auto csIds = request->copysetid();
  response->set_statuscode(TopoStatusCode::TOPO_OK);
  for (auto id : csIds) {
    CopySetKey key(poolId, id);
    CopySetInfo info;
    if (topology_->GetCopySet(key, &info)) {
      pb::mds::topology::CopySetServerInfo* serverInfo = response->add_csinfo();
      serverInfo->set_copysetid(id);
      for (auto metaserverId : info.GetCopySetMembers()) {
        MetaServer metaserver;
        if (topology_->GetMetaServer(metaserverId, &metaserver)) {
          pb::mds::topology::MetaServerLocation* location =
              serverInfo->add_cslocs();
          location->set_metaserverid(metaserver.GetId());
          location->set_internalip(metaserver.GetInternalIp());
          location->set_internalport(metaserver.GetInternalPort());
          location->set_externalip(metaserver.GetExternalIp());
          location->set_externalport(metaserver.GetExternalPort());
        } else {
          LOG(INFO) << "GetMetaserver failed"
                    << " when GetMetaServerListInCopysets.";
          response->set_statuscode(TopoStatusCode::TOPO_INTERNAL_ERROR);
          return;
        }
      }
    } else {
      LOG(ERROR) << "GetCopyset failed when GetMetaServerListInCopysets.";
      response->set_statuscode(TopoStatusCode::TOPO_COPYSET_NOT_FOUND);
      return;
    }
  }
}

void TopologyManager::ListPartition(
    const pb::mds::topology::ListPartitionRequest* request,
    pb::mds::topology::ListPartitionResponse* response) {
  FsIdType fsId = request->fsid();
  auto partitionInfoList = response->mutable_partitioninfolist();
  response->set_statuscode(TopoStatusCode::TOPO_OK);
  std::list<Partition> partitions = topology_->GetPartitionOfFs(fsId);

  for (auto partition : partitions) {
    pb::common::PartitionInfo* info = partitionInfoList->Add();
    info->set_fsid(partition.GetFsId());
    info->set_poolid(partition.GetPoolId());
    info->set_copysetid(partition.GetCopySetId());
    info->set_partitionid(partition.GetPartitionId());
    info->set_start(partition.GetIdStart());
    info->set_end(partition.GetIdEnd());
    info->set_txid(partition.GetTxId());
    info->set_status(partition.GetStatus());
    info->set_inodenum(partition.GetInodeNum());
    info->set_dentrynum(partition.GetDentryNum());
    if (partition.GetIdNext() != 0) {
      info->set_nextid(partition.GetIdNext());
    }
  }
}

void TopologyManager::GetLatestPartitionsTxId(
    const std::vector<pb::mds::topology::PartitionTxId>& txIds,
    std::vector<pb::mds::topology::PartitionTxId>* needUpdate) {
  for (auto iter = txIds.begin(); iter != txIds.end(); iter++) {
    Partition out;
    topology_->GetPartition(iter->partitionid(), &out);
    if (out.GetTxId() != iter->txid()) {
      pb::mds::topology::PartitionTxId tmp;
      tmp.set_partitionid(iter->partitionid());
      tmp.set_txid(out.GetTxId());
      needUpdate->push_back(std::move(tmp));
    }
  }
}

void TopologyManager::ListPartitionOfFs(
    FsIdType fsId, std::list<pb::common::PartitionInfo>* list) {
  for (auto& partition : topology_->GetPartitionOfFs(fsId)) {
    list->emplace_back(partition.ToPartitionInfo());
  }

  return;
}

void TopologyManager::GetCopysetOfPartition(
    const pb::mds::topology::GetCopysetOfPartitionRequest* request,
    pb::mds::topology::GetCopysetOfPartitionResponse* response) {
  for (int i = 0; i < request->partitionid_size(); i++) {
    PartitionIdType pId = request->partitionid(i);
    CopySetInfo copyset;
    if (topology_->GetCopysetOfPartition(pId, &copyset)) {
      pb::mds::topology::Copyset cs;
      cs.set_poolid(copyset.GetPoolId());
      cs.set_copysetid(copyset.GetId());
      // get coptset members
      for (auto msId : copyset.GetCopySetMembers()) {
        MetaServer ms;
        if (topology_->GetMetaServer(msId, &ms)) {
          pb::common::Peer* peer = cs.add_peers();
          peer->set_id(ms.GetId());
          peer->set_address(
              BuildPeerIdWithIpPort(ms.GetInternalIp(), ms.GetInternalPort()));
        } else {
          LOG(ERROR) << "GetMetaServer failed, id = " << msId;
          response->set_statuscode(TopoStatusCode::TOPO_METASERVER_NOT_FOUND);
          response->clear_copysetmap();
          return;
        }
      }
      (*response->mutable_copysetmap())[pId] = cs;
    } else {
      LOG(ERROR) << "GetCopysetOfPartition failed. partitionId = " << pId;
      response->set_statuscode(TopoStatusCode::TOPO_COPYSET_NOT_FOUND);
      response->clear_copysetmap();
      return;
    }
  }
  response->set_statuscode(TopoStatusCode::TOPO_OK);
}

TopoStatusCode TopologyManager::GetCopysetMembers(
    const PoolIdType poolId, const CopySetIdType copysetId,
    std::set<std::string>* addrs) {
  CopySetKey key(poolId, copysetId);
  CopySetInfo info;
  if (topology_->GetCopySet(key, &info)) {
    for (auto metaserverId : info.GetCopySetMembers()) {
      MetaServer server;
      if (topology_->GetMetaServer(metaserverId, &server)) {
        std::string addr = server.GetExternalIp() + ":" +
                           std::to_string(server.GetExternalPort());
        addrs->emplace(addr);
      } else {
        LOG(ERROR) << "GetMetaserver failed,"
                   << " metaserverId =" << metaserverId;
        return TopoStatusCode::TOPO_METASERVER_NOT_FOUND;
      }
    }
  } else {
    LOG(ERROR) << "Get copyset failed." << " poolId = " << poolId
               << ", copysetId = " << copysetId;
    return TopoStatusCode::TOPO_COPYSET_NOT_FOUND;
  }
  return TopoStatusCode::TOPO_OK;
}

void TopologyManager::GetCopysetInfo(
    const uint32_t& poolId, const uint32_t& copysetId,
    pb::mds::topology::CopysetValue* copysetValue) {
  // default is ok, when find error set to error code
  copysetValue->set_statuscode(TopoStatusCode::TOPO_OK);
  CopySetKey key(poolId, copysetId);
  CopySetInfo info;
  if (topology_->GetCopySet(key, &info)) {
    auto* valueCopysetInfo = new pb::mds::heartbeat::CopySetInfo();
    valueCopysetInfo->set_poolid(info.GetPoolId());
    valueCopysetInfo->set_copysetid(info.GetId());
    // set peers
    for (auto const& msId : info.GetCopySetMembers()) {
      MetaServer ms;
      if (topology_->GetMetaServer(msId, &ms)) {
        pb::common::Peer* peer = valueCopysetInfo->add_peers();
        peer->set_id(ms.GetId());
        peer->set_address(
            BuildPeerIdWithIpPort(ms.GetInternalIp(), ms.GetInternalPort()));
      } else {
        LOG(ERROR) << "perrs: poolId=" << poolId << " copysetid=" << copysetId
                   << " has metaServer error, metaserverId = " << msId;
        copysetValue->set_statuscode(TopoStatusCode::TOPO_METASERVER_NOT_FOUND);
      }
    }
    valueCopysetInfo->set_epoch(info.GetEpoch());

    // set leader peer
    auto msId = info.GetLeader();
    MetaServer ms;
    auto* peer = new pb::common::Peer();
    if (topology_->GetMetaServer(msId, &ms)) {
      peer->set_id(ms.GetId());
      peer->set_address(
          BuildPeerIdWithIpPort(ms.GetInternalIp(), ms.GetInternalPort()));
    } else {
      LOG(WARNING) << "leaderpeer: poolId=" << poolId
                   << " copysetid=" << copysetId
                   << " has metaServer error, metaserverId = " << msId;
      copysetValue->set_statuscode(TopoStatusCode::TOPO_METASERVER_NOT_FOUND);
    }
    valueCopysetInfo->set_allocated_leaderpeer(peer);

    // set partitioninfolist
    for (auto const& i : info.GetPartitionIds()) {
      Partition tmp;
      if (!topology_->GetPartition(i, &tmp)) {
        LOG(WARNING) << "poolId=" << poolId << " copysetid=" << copysetId
                     << " has pattition error, partitionId=" << i;
        copysetValue->set_statuscode(TopoStatusCode::TOPO_PARTITION_NOT_FOUND);
      } else {
        auto partition = valueCopysetInfo->add_partitioninfolist();
        partition->set_fsid(tmp.GetFsId());
        partition->set_poolid(tmp.GetPoolId());
        partition->set_copysetid(tmp.GetCopySetId());
        partition->set_partitionid(tmp.GetPartitionId());
        partition->set_start(tmp.GetIdStart());
        partition->set_end(tmp.GetIdEnd());
        partition->set_txid(tmp.GetTxId());
        partition->set_status(tmp.GetStatus());
        partition->set_inodenum(tmp.GetInodeNum());
        partition->set_dentrynum(tmp.GetDentryNum());
        if (tmp.GetIdNext() != 0) {
          partition->set_nextid(tmp.GetIdNext());
        }
      }
    }

    copysetValue->set_allocated_copysetinfo(valueCopysetInfo);
  } else {
    LOG(ERROR) << "Get copyset failed." << " poolId=" << poolId
               << " copysetId=" << copysetId;
    copysetValue->set_statuscode(TopoStatusCode::TOPO_COPYSET_NOT_FOUND);
  }
}

void TopologyManager::GetCopysetsInfo(
    const pb::mds::topology::GetCopysetsInfoRequest* request,
    pb::mds::topology::GetCopysetsInfoResponse* response) {
  for (auto const& i : request->copysetkeys()) {
    GetCopysetInfo(i.poolid(), i.copysetid(), response->add_copysetvalues());
  }
}

void TopologyManager::ListCopysetsInfo(
    pb::mds::topology::ListCopysetInfoResponse* response) {
  auto cpysetInfoVec = topology_->ListCopysetInfo();
  for (auto const& i : cpysetInfoVec) {
    auto* copysetValue = response->add_copysetvalues();
    // default is ok, when find error set to error code
    copysetValue->set_statuscode(TopoStatusCode::TOPO_OK);
    auto* value_copyset_info = new pb::mds::heartbeat::CopySetInfo();
    value_copyset_info->set_poolid(i.GetPoolId());
    value_copyset_info->set_copysetid(i.GetId());
    // set peers
    for (auto const& msId : i.GetCopySetMembers()) {
      MetaServer ms;
      if (topology_->GetMetaServer(msId, &ms)) {
        pb::common::Peer* peer = value_copyset_info->add_peers();
        peer->set_id(ms.GetId());
        peer->set_address(
            BuildPeerIdWithIpPort(ms.GetInternalIp(), ms.GetInternalPort()));
      } else {
        LOG(ERROR) << "perrs: poolId=" << i.GetPoolId()
                   << " copysetid=" << i.GetId()
                   << " has metaServer error, metaserverId = " << msId;
        copysetValue->set_statuscode(TopoStatusCode::TOPO_METASERVER_NOT_FOUND);
      }
    }
    value_copyset_info->set_epoch(i.GetEpoch());

    // set leader peer
    auto msId = i.GetLeader();
    MetaServer ms;
    auto* peer = new pb::common::Peer();
    if (topology_->GetMetaServer(msId, &ms)) {
      peer->set_id(ms.GetId());
      peer->set_address(
          BuildPeerIdWithIpPort(ms.GetInternalIp(), ms.GetInternalPort()));
    } else {
      LOG(WARNING) << "leaderpeer: poolId=" << i.GetPoolId()
                   << " copysetid=" << i.GetId()
                   << " has metaServer error, metaserverId = " << msId;
      copysetValue->set_statuscode(TopoStatusCode::TOPO_METASERVER_NOT_FOUND);
    }
    value_copyset_info->set_allocated_leaderpeer(peer);

    // set partitioninfolist
    for (auto const& j : i.GetPartitionIds()) {
      Partition tmp;
      if (!topology_->GetPartition(j, &tmp)) {
        LOG(WARNING) << "poolId=" << i.GetPoolId() << " copysetid=" << i.GetId()
                     << " has pattition error, partitionId=" << j;
        copysetValue->set_statuscode(TopoStatusCode::TOPO_PARTITION_NOT_FOUND);
      } else {
        *value_copyset_info->add_partitioninfolist() =
            std::move(pb::common::PartitionInfo(tmp));
      }
    }

    copysetValue->set_allocated_copysetinfo(value_copyset_info);
  }
}

void TopologyManager::GetMetaServersSpace(
    ::google::protobuf::RepeatedPtrField<pb::mds::topology::MetadataUsage>*
        spaces) {
  topology_->GetMetaServersSpace(spaces);
}

void TopologyManager::GetTopology(
    pb::mds::topology::ListTopologyResponse* response) {
  // cluster info
  ClusterInformation info;
  if (topology_->GetClusterInfo(&info)) {
    response->set_clusterid(info.clusterId);
  } else {
    response->set_clusterid("unknown");
  }

  ListPool(nullptr, response->mutable_pools());
  ListZone(response->mutable_zones());
  ListServer(response->mutable_servers());
  ListMetaserverOfCluster(response->mutable_metaservers());
}

void TopologyManager::ListZone(pb::mds::topology::ListZoneResponse* response) {
  response->set_statuscode(TopoStatusCode::TOPO_OK);
  auto zoneIdVec = topology_->GetZoneInCluster();
  for (auto const& zoneId : zoneIdVec) {
    Zone zone;
    if (topology_->GetZone(zoneId, &zone)) {
      auto zoneInfo = response->add_zoneinfos();
      zoneInfo->set_zoneid(zone.GetId());
      zoneInfo->set_zonename(zone.GetName());
      zoneInfo->set_poolid(zone.GetPoolId());
    } else {
      LOG(ERROR) << "Topology has counter an internal error: "
                 << "[func:] ListZone, "
                 << "[msg:] Zone not found, id = " << zoneId;
      response->set_statuscode(TopoStatusCode::TOPO_INTERNAL_ERROR);
    }
  }
}

void TopologyManager::ListServer(
    pb::mds::topology::ListServerResponse* response) {
  response->set_statuscode(TopoStatusCode::TOPO_OK);
  auto serverIdVec = topology_->GetServerInCluster();
  for (auto const& serverId : serverIdVec) {
    Server server;
    if (topology_->GetServer(serverId, &server)) {
      auto serverInfo = response->add_serverinfos();
      serverInfo->set_serverid(server.GetId());
      serverInfo->set_hostname(server.GetHostName());
      serverInfo->set_internalip(server.GetInternalIp());
      serverInfo->set_internalport(server.GetInternalPort());
      serverInfo->set_externalip(server.GetExternalIp());
      serverInfo->set_externalport(server.GetExternalPort());
      serverInfo->set_zoneid(server.GetZoneId());
      serverInfo->set_poolid(server.GetPoolId());
    } else {
      LOG(ERROR) << "Topology has counter an internal error: "
                 << "[func:] ListServer, "
                 << "[msg:] Server not found, id = " << serverId;
      response->set_statuscode(TopoStatusCode::TOPO_INTERNAL_ERROR);
    }
  }
}

void TopologyManager::ListMetaserverOfCluster(
    pb::mds::topology::ListMetaServerResponse* response) {
  response->set_statuscode(TopoStatusCode::TOPO_OK);
  auto metaserverIdList = topology_->GetMetaServerInCluster();
  for (auto const& id : metaserverIdList) {
    MetaServer ms;
    if (topology_->GetMetaServer(id, &ms)) {
      pb::mds::topology::MetaServerInfo* msInfo =
          response->add_metaserverinfos();
      msInfo->set_metaserverid(ms.GetId());
      msInfo->set_hostname(ms.GetHostName());
      msInfo->set_internalip(ms.GetInternalIp());
      msInfo->set_internalport(ms.GetInternalPort());
      msInfo->set_externalip(ms.GetExternalIp());
      msInfo->set_externalport(ms.GetExternalPort());
      msInfo->set_onlinestate(ms.GetOnlineState());
      msInfo->set_serverid(ms.GetServerId());
    } else {
      LOG(ERROR) << "Topology has counter an internal error: "
                 << "[func:] ListMetaServerOfCluster, "
                 << "[msg:] metaserver not found, id = " << id;
      response->set_statuscode(TopoStatusCode::TOPO_INTERNAL_ERROR);
      return;
    }
  }
}

TopoStatusCode TopologyManager::UpdatePartitionStatus(
    PartitionIdType partition_id, pb::common::PartitionStatus status) {
  return topology_->UpdatePartitionStatus(partition_id, status);
}

void TopologyManager::RegistMemcacheCluster(
    const pb::mds::topology::RegistMemcacheClusterRequest* request,
    pb::mds::topology::RegistMemcacheClusterResponse* response) {
  response->set_statuscode(TopoStatusCode::TOPO_OK);
  // register memcacheCluster as server
  WriteLockGuard lock(registMemcacheClusterMutex_);

  // idempotence
  std::list<MemcacheCluster> cluster_list = topology_->ListMemcacheClusters();
  MemcacheCluster m_cluster(
      0, std::list<MemcacheServer>(request->servers().begin(),
                                   request->servers().end()));
  for (auto const& cluster : cluster_list) {
    m_cluster.SetId(cluster.GetId());
    if (cluster == m_cluster) {
      // has registered memcache cluster
      response->set_clusterid(cluster.GetId());
      return;
    }
  }

  // Guarantee the uniqueness of memcacheServer
  std::list<MemcacheServer> server_registed = topology_->ListMemcacheServers();
  std::list<MemcacheServer> server_list;
  for (auto const& server : request->servers()) {
    auto cmp = [server](const MemcacheServer& ms) { return ms == server; };
    if (std::find_if(server_registed.begin(), server_registed.end(), cmp) !=
        server_registed.end()) {
      LOG(ERROR) << "Regist MemcacheCluster failed! Server["
                 << server.ShortDebugString()
                 << "] already existsin another cluster";
      response->set_statuscode(TopoStatusCode::TOPO_IP_PORT_DUPLICATED);
      break;
    }
    server_list.emplace_back(server);
  }

  if (response->statuscode() == TopoStatusCode::TOPO_OK) {
    // add new cluster
    MemcacheClusterIdType id = topology_->AllocateMemCacheClusterId();
    if (id == static_cast<MemcacheClusterIdType>(UNINITIALIZE_ID)) {
      response->set_statuscode(TopoStatusCode::TOPO_ALLOCATE_ID_FAIL);
    } else {
      MemcacheCluster cluster(id, std::move(server_list));
      TopoStatusCode error_code =
          topology_->AddMemcacheCluster(std::move(cluster));
      response->set_statuscode(error_code);
      response->set_clusterid(id);
    }
  }
}

void TopologyManager::ListMemcacheCluster(
    pb::mds::topology::ListMemcacheClusterResponse* response) {
  std::list<MemcacheCluster> cluster_list = topology_->ListMemcacheClusters();
  if (!cluster_list.empty()) {
    response->set_statuscode(TopoStatusCode::TOPO_OK);
    for (auto& cluster : cluster_list) {
      (*response->add_memcacheclusters()) = std::move(cluster);
    }
  } else {
    response->set_statuscode(TopoStatusCode::TOPO_MEMCACHECLUSTER_NOT_FOUND);
  }
}

void TopologyManager::AllocOrGetMemcacheCluster(
    const pb::mds::topology::AllocOrGetMemcacheClusterRequest* request,
    pb::mds::topology::AllocOrGetMemcacheClusterResponse* response) {
  auto status_code = topology_->AllocOrGetMemcacheCluster(
      request->fsid(), response->mutable_cluster());
  response->set_statuscode(status_code);
}

}  // namespace topology
}  // namespace mds
}  // namespace dingofs
