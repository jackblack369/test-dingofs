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
 * Created Date: 2021-08-24
 * Author: wanghai01
 */

#ifndef DINGOFS_SRC_MDS_TOPOLOGY_TOPOLOGY_ITEM_H_
#define DINGOFS_SRC_MDS_TOPOLOGY_TOPOLOGY_ITEM_H_

#include <algorithm>
#include <cstdint>
#include <list>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>

#include "dingofs/common.pb.h"
#include "dingofs/metaserver.pb.h"
#include "dingofs/topology.pb.h"
#include "mds/topology/topology_id_generator.h"
#include "utils/concurrent/concurrent.h"

namespace dingofs {
namespace mds {
namespace topology {

using FileType = pb::metaserver::FsFileType;
using ProtoFileType2InodeNumMap = ::google::protobuf::Map<int32_t, int64_t>;

/**
 * @brief cluster information, so far we only use clusterId
 */
struct ClusterInformation {
  // the only and unique Id of a cluster
  std::string clusterId;
  // <fsId, partition index of this fs>
  std::map<uint32_t, uint32_t> partitionIndexs;

  ClusterInformation() = default;
  explicit ClusterInformation(const std::string& clusterId)
      : clusterId(clusterId) {}

  // all partition number include deleted
  uint32_t GetPartitionIndexOfFS(uint32_t fsId) {
    return partitionIndexs[fsId];
  }

  // for upgrade to keep compatibility
  void UpdatePartitionIndexOfFs(uint32_t fsId, uint32_t number) {
    if (number > partitionIndexs[fsId]) {
      partitionIndexs[fsId] = number;
    }
  }

  void AddPartitionIndexOfFs(uint32_t fsId) { partitionIndexs[fsId]++; }

  bool SerializeToString(std::string* value) const;

  bool ParseFromString(const std::string& value);
};

class Pool {
 public:
  struct RedundanceAndPlaceMentPolicy {
    uint16_t replicaNum;
    uint32_t copysetNum;
    uint16_t zoneNum;
  };

 public:
  static bool TransRedundanceAndPlaceMentPolicyFromJsonStr(
      const std::string& jsonStr, RedundanceAndPlaceMentPolicy* rap);

 public:
  Pool() : id_(UNINITIALIZE_ID), name_(""), createTime_(0), diskCapacity_(0) {}
  Pool(PoolIdType id, const std::string& name,
       const RedundanceAndPlaceMentPolicy& rap, uint64_t createTime)
      : id_(id),
        name_(name),
        rap_(rap),
        createTime_(createTime),
        diskCapacity_(0) {}

  PoolIdType GetId() const { return id_; }

  std::string GetName() const { return name_; }

  void SetRedundanceAndPlaceMentPolicy(
      const RedundanceAndPlaceMentPolicy& rap) {
    rap_ = rap;
  }

  bool SetRedundanceAndPlaceMentPolicyByJson(const std::string& jsonStr);

  RedundanceAndPlaceMentPolicy GetRedundanceAndPlaceMentPolicy() const {
    return rap_;
  }

  std::string GetRedundanceAndPlaceMentPolicyJsonStr() const;

  uint16_t GetReplicaNum() const { return rap_.replicaNum; }

  uint64_t GetCreateTime() const { return createTime_; }

  void SetDiskThreshold(uint64_t diskThreshold) {
    diskCapacity_ = diskThreshold;
  }
  uint64_t GetDiskThreshold() const { return diskCapacity_; }

  void AddZone(ZoneIdType id) { zoneList_.push_back(id); }

  void RemoveZone(ZoneIdType id) { zoneList_.remove(id); }

  std::list<ZoneIdType> GetZoneList() const { return zoneList_; }

  bool SerializeToString(std::string* value) const;

  bool ParseFromString(const std::string& value);

 private:
  PoolIdType id_;
  std::string name_;
  RedundanceAndPlaceMentPolicy rap_;
  uint64_t createTime_;
  uint64_t diskCapacity_;

  std::list<ZoneIdType> zoneList_;
};

class Zone {
 public:
  Zone() : id_(UNINITIALIZE_ID), name_(""), poolId_(UNINITIALIZE_ID) {}
  Zone(PoolIdType id, const std::string& name, PoolIdType poolId)
      : id_(id), name_(name), poolId_(poolId) {}

  ZoneIdType GetId() const { return id_; }

  std::string GetName() const { return name_; }

  PoolIdType GetPoolId() const { return poolId_; }

  void AddServer(ServerIdType id) { serverList_.push_back(id); }

  void RemoveServer(ServerIdType id) { serverList_.remove(id); }

  std::list<ServerIdType> GetServerList() const { return serverList_; }

  bool SerializeToString(std::string* value) const;

  bool ParseFromString(const std::string& value);

 private:
  ZoneIdType id_;
  std::string name_;
  PoolIdType poolId_;

  std::list<ServerIdType> serverList_;
};

class Server {
 public:
  Server()
      : id_(UNINITIALIZE_ID),
        hostName_(""),
        internalIp_(""),
        internalPort_(0),
        externalIp_(""),
        externalPort_(0),
        zoneId_(UNINITIALIZE_ID),
        poolId_(UNINITIALIZE_ID) {}
  Server(ServerIdType id, const std::string& hostName,
         const std::string& internalIp, uint32_t internalPort,
         const std::string& externalIp, uint32_t externalPort,
         ZoneIdType zoneId, PoolIdType poolId)
      : id_(id),
        hostName_(hostName),
        internalIp_(internalIp),
        internalPort_(internalPort),
        externalIp_(externalIp),
        externalPort_(externalPort),
        zoneId_(zoneId),
        poolId_(poolId) {}

  ServerIdType GetId() const { return id_; }

  std::string GetHostName() const { return hostName_; }

  std::string GetInternalIp() const { return internalIp_; }

  uint32_t GetInternalPort() const { return internalPort_; }

  std::string GetExternalIp() const { return externalIp_; }

  uint32_t GetExternalPort() const { return externalPort_; }

  ZoneIdType GetZoneId() const { return zoneId_; }

  PoolIdType GetPoolId() const { return poolId_; }

  void AddMetaServer(MetaServerIdType id) { metaserverList_.push_back(id); }

  void RemoveMetaServer(MetaServerIdType id) { metaserverList_.remove(id); }

  std::list<MetaServerIdType> GetMetaServerList() const {
    return metaserverList_;
  }

  bool SerializeToString(std::string* value) const;

  bool ParseFromString(const std::string& value);

 private:
  ServerIdType id_;
  std::string hostName_;
  std::string internalIp_;
  uint32_t internalPort_;
  std::string externalIp_;
  uint32_t externalPort_;
  ZoneIdType zoneId_;
  PoolIdType poolId_;

  std::list<MetaServerIdType> metaserverList_;
};

class MetaServerSpace {
 public:
  // for test only
  MetaServerSpace(uint64_t diskThreshold = 0, uint64_t diskUsed = 0,
                  uint64_t diskCopysetMinRequire = 0,
                  uint64_t memoryThreshold = 0, uint64_t memoryUsed = 0,
                  uint64_t memoryCopySetMinRequire = 0) {
    memoryThresholdByte_ = memoryThreshold;
    memoryCopySetMinRequireByte_ = memoryCopySetMinRequire;
    memoryUsedByte_ = memoryUsed;
    diskThresholdByte_ = diskThreshold;
    diskCopysetMinRequireByte_ = diskCopysetMinRequire;
    diskUsedByte_ = diskUsed;
  }

  explicit MetaServerSpace(pb::mds::heartbeat::MetaServerSpaceStatus status) {
    SetSpaceStatus(status);
  }

  void SetDiskThreshold(uint64_t threshold) { diskThresholdByte_ = threshold; }
  uint64_t GetDiskThreshold() const { return diskThresholdByte_; }
  void SetDiskUsed(uint64_t diskUsed) { diskUsedByte_ = diskUsed; }
  uint64_t GetDiskUsed() const { return diskUsedByte_; }
  void SetDiskMinRequire(uint64_t require) {
    diskCopysetMinRequireByte_ = require;
  }
  uint64_t GetDiskMinRequire() const { return diskCopysetMinRequireByte_; }

  void SetMemoryThreshold(uint64_t threshold) {
    memoryThresholdByte_ = threshold;
  }
  uint64_t GetMemoryThreshold() const { return memoryThresholdByte_; }
  void SetMemoryUsed(uint64_t memoryUsed) { memoryUsedByte_ = memoryUsed; }
  uint64_t GetMemoryUsed() const { return memoryUsedByte_; }
  void SetMemoryMinRequire(uint64_t require) {
    memoryCopySetMinRequireByte_ = require;
  }
  uint64_t GetMemoryMinRequire() const { return memoryCopySetMinRequireByte_; }

  void SetSpaceStatus(pb::mds::heartbeat::MetaServerSpaceStatus status) {
    diskThresholdByte_ = status.diskthresholdbyte();
    diskCopysetMinRequireByte_ = status.diskcopysetminrequirebyte();
    diskUsedByte_ = status.diskusedbyte();
    memoryThresholdByte_ = status.memorythresholdbyte();
    memoryCopySetMinRequireByte_ = status.memorycopysetminrequirebyte();
    memoryUsedByte_ = status.memoryusedbyte();
  }

  double GetResourceUseRatioPercent() {
    double diskUseRatio = 0;
    if (diskThresholdByte_ != 0) {
      diskUseRatio = 100.0 * diskUsedByte_ / diskThresholdByte_;
    }

    return diskUseRatio;
  }

  bool IsMetaserverResourceAvailable() {
    if (diskThresholdByte_ <= diskUsedByte_ ||
        diskThresholdByte_ < (diskCopysetMinRequireByte_ + diskUsedByte_)) {
      return false;
    }

    return true;
  }

  // only consider the disk usage
  bool IsResourceOverload() { return diskThresholdByte_ < diskUsedByte_; }

 private:
  uint64_t memoryThresholdByte_;
  uint64_t memoryCopySetMinRequireByte_;
  uint64_t memoryUsedByte_;

  uint64_t diskThresholdByte_;
  uint64_t diskCopysetMinRequireByte_;
  uint64_t diskUsedByte_;
};

class MetaServer {
 public:
  MetaServer()
      : id_(UNINITIALIZE_ID),
        hostName_(""),
        token_(""),
        serverId_(UNINITIALIZE_ID),
        internalIp_(""),
        internalPort_(0),
        externalIp_(""),
        externalPort_(0),
        startUpTime_(0),
        onlineState_(pb::mds::topology::OnlineState::OFFLINE),
        dirty_(false) {}

  MetaServer(MetaServerIdType id, const std::string& hostName,
             const std::string& token, ServerIdType serverId,
             const std::string& internalIp, uint32_t internalPort,
             const std::string& externalIp, uint32_t externalPort,
             pb::mds::topology::OnlineState onlineState =
                 pb::mds::topology::OnlineState::OFFLINE)
      : id_(id),
        hostName_(hostName),
        token_(token),
        serverId_(serverId),
        internalIp_(internalIp),
        internalPort_(internalPort),
        externalIp_(externalIp),
        externalPort_(externalPort),
        startUpTime_(0),
        onlineState_(onlineState),
        dirty_(false) {}

  MetaServer(const MetaServer& v)
      : id_(v.id_),
        hostName_(v.hostName_),
        token_(v.token_),
        serverId_(v.serverId_),
        internalIp_(v.internalIp_),
        internalPort_(v.internalPort_),
        externalIp_(v.externalIp_),
        externalPort_(v.externalPort_),
        startUpTime_(v.startUpTime_),
        onlineState_(v.onlineState_),
        space_(v.space_),
        dirty_(v.dirty_) {}

  MetaServer& operator=(const MetaServer& v) {
    if (&v == this) {
      return *this;
    }
    id_ = v.id_;
    hostName_ = v.hostName_;
    token_ = v.token_;
    serverId_ = v.serverId_;
    internalIp_ = v.internalIp_;
    internalPort_ = v.internalPort_;
    externalIp_ = v.externalIp_;
    externalPort_ = v.externalPort_;
    startUpTime_ = v.startUpTime_;
    onlineState_ = v.onlineState_;
    space_ = v.space_;
    dirty_ = v.dirty_;
    return *this;
  }

  MetaServerIdType GetId() const { return id_; }

  std::string GetHostName() const { return hostName_; }

  std::string GetToken() const { return token_; }

  void SetToken(std::string token) { token_ = token; }

  void SetServerId(ServerIdType id) { serverId_ = id; }

  ServerIdType GetServerId() const { return serverId_; }

  std::string GetInternalIp() const { return internalIp_; }

  uint32_t GetInternalPort() const { return internalPort_; }

  void SetInternalIp(std::string internalIp) { internalIp_ = internalIp; }

  void SetInternalPort(uint32_t internalPort) { internalPort_ = internalPort; }

  std::string GetExternalIp() const { return externalIp_; }

  uint32_t GetExternalPort() const { return externalPort_; }

  void SetStartUpTime(uint64_t time) { startUpTime_ = time; }

  uint64_t GetStartUpTime() const { return startUpTime_; }

  void SetOnlineState(pb::mds::topology::OnlineState state) {
    onlineState_ = state;
  }

  pb::mds::topology::OnlineState GetOnlineState() const { return onlineState_; }

  void SetMetaServerSpace(const MetaServerSpace& space) { space_ = space; }

  MetaServerSpace GetMetaServerSpace() const { return space_; }

  bool GetDirtyFlag() const { return dirty_; }

  void SetDirtyFlag(bool dirty) { dirty_ = dirty; }

  ::dingofs::utils::RWLock& GetRWLockRef() const { return mutex_; }

  bool SerializeToString(std::string* value) const;

  bool ParseFromString(const std::string& value);

 private:
  MetaServerIdType id_;
  std::string hostName_;
  std::string token_;
  ServerIdType serverId_;
  std::string internalIp_;
  uint32_t internalPort_;
  std::string externalIp_;
  uint32_t externalPort_;
  uint64_t startUpTime_;
  pb::mds::topology::OnlineState onlineState_;  // 0:online、1: offline
  MetaServerSpace space_;
  bool dirty_;
  mutable ::dingofs::utils::RWLock mutex_;
};

using CopySetKey = std::pair<PoolIdType, CopySetIdType>;

struct CopysetIdInfo {
  PoolIdType poolId;
  CopySetIdType copySetId;
};

class CopySetInfo {
 public:
  CopySetInfo()
      : poolId_(UNINITIALIZE_ID),
        copySetId_(UNINITIALIZE_ID),
        leader_(UNINITIALIZE_ID),
        epoch_(0),
        hasCandidate_(false),
        candidate_(UNINITIALIZE_ID),
        dirty_(false),
        available_(true) {}

  CopySetInfo(PoolIdType poolId, CopySetIdType id)
      : poolId_(poolId),
        copySetId_(id),
        leader_(UNINITIALIZE_ID),
        epoch_(0),
        hasCandidate_(false),
        candidate_(UNINITIALIZE_ID),
        dirty_(false),
        available_(true) {}

  CopySetInfo(const CopySetInfo& v)
      : poolId_(v.poolId_),
        copySetId_(v.copySetId_),
        leader_(v.leader_),
        epoch_(v.epoch_),
        peers_(v.peers_),
        partitionIds_(v.partitionIds_),
        hasCandidate_(v.hasCandidate_),
        candidate_(v.candidate_),
        dirty_(v.dirty_),
        available_(v.available_) {}

  CopySetInfo& operator=(const CopySetInfo& v) {
    if (&v == this) {
      return *this;
    }
    poolId_ = v.poolId_;
    copySetId_ = v.copySetId_;
    leader_ = v.leader_;
    epoch_ = v.epoch_;
    peers_ = v.peers_;
    partitionIds_ = v.partitionIds_;
    hasCandidate_ = v.hasCandidate_;
    candidate_ = v.candidate_;
    dirty_ = v.dirty_;
    available_ = v.available_;
    return *this;
  }

  void SetPoolId(PoolIdType poolId) { poolId_ = poolId; }

  PoolIdType GetPoolId() const { return poolId_; }

  void SetCopySetId(CopySetIdType copySetId) { copySetId_ = copySetId; }

  CopySetIdType GetId() const { return copySetId_; }

  void SetEpoch(EpochType epoch) { epoch_ = epoch; }

  EpochType GetEpoch() const { return epoch_; }

  MetaServerIdType GetLeader() const { return leader_; }

  void SetLeader(MetaServerIdType leader) { leader_ = leader; }

  CopySetKey GetCopySetKey() const { return CopySetKey(poolId_, copySetId_); }

  std::set<MetaServerIdType> GetCopySetMembers() const { return peers_; }

  std::string GetCopySetMembersStr() const;

  void SetCopySetMembers(const std::set<MetaServerIdType>& peers) {
    peers_ = peers;
  }

  bool HasMember(MetaServerIdType peer) const { return peers_.count(peer) > 0; }

  bool SetCopySetMembersByJson(const std::string& jsonStr);

  uint64_t GetPartitionNum() const { return partitionIds_.size(); }

  bool HasCandidate() const { return hasCandidate_; }

  void SetCandidate(MetaServerIdType id) {
    hasCandidate_ = true;
    candidate_ = id;
  }

  MetaServerIdType GetCandidate() const {
    if (hasCandidate_) {
      return candidate_;
    } else {
      return UNINITIALIZE_ID;
    }
  }

  void ClearCandidate() { hasCandidate_ = false; }

  bool GetDirtyFlag() const { return dirty_; }

  void SetDirtyFlag(bool dirty) { dirty_ = dirty; }

  bool IsAvailable() const { return available_; }

  void SetAvailableFlag(bool aval) { available_ = aval; }

  ::dingofs::utils::RWLock& GetRWLockRef() const { return mutex_; }

  bool SerializeToString(std::string* value) const;

  bool ParseFromString(const std::string& value);

  void AddPartitionId(const PartitionIdType& id) { partitionIds_.insert(id); }

  void RemovePartitionId(PartitionIdType id) { partitionIds_.erase(id); }

  const std::set<PartitionIdType>& GetPartitionIds() const {
    return partitionIds_;
  }

 private:
  PoolIdType poolId_;
  CopySetIdType copySetId_;
  MetaServerIdType leader_;
  EpochType epoch_;
  std::set<MetaServerIdType> peers_;
  std::set<PartitionIdType> partitionIds_;
  bool hasCandidate_;
  MetaServerIdType candidate_;

  /**
   * @brief to mark whether data is dirty, for writing to storage regularly
   */
  bool dirty_;

  /**
   * @brief To mark whether the copyset is available. If not available,
   *        will stop allocating chunks into this copyset.
   */
  bool available_;

  /**
   * @brief metaserver read/write lock, for protecting concurrent
   *        read/write on the copyset
   */
  mutable ::dingofs::utils::RWLock mutex_;
};

struct PartitionStatistic {
  pb::common::PartitionStatus status;
  uint64_t inodeNum;
  uint64_t dentryNum;
  uint64_t nextId;
  std::unordered_map<FileType, uint64_t> fileType2InodeNum;
};

class Partition {
 public:
  Partition()
      : fsId_(UNINITIALIZE_ID),
        poolId_(UNINITIALIZE_ID),
        copySetId_(UNINITIALIZE_ID),
        partitionId_(UNINITIALIZE_ID),
        idStart_(0),
        idEnd_(0),
        idNext_(0),
        txId_(0),
        status_(pb::common::PartitionStatus::READWRITE),
        inodeNum_(0),
        dentryNum_(0) {
    InitFileType2InodeNum();
  }

  Partition(FsIdType fsId, PoolIdType poolId, CopySetIdType copySetId,
            PartitionIdType partitionId, uint64_t idStart, uint64_t idEnd)
      : fsId_(fsId),
        poolId_(poolId),
        copySetId_(copySetId),
        partitionId_(partitionId),
        idStart_(idStart),
        idEnd_(idEnd),
        idNext_(0),
        txId_(0),
        status_(pb::common::PartitionStatus::READWRITE),
        inodeNum_(0),
        dentryNum_(0) {
    InitFileType2InodeNum();
  }

  Partition(const Partition& v)
      : fsId_(v.fsId_),
        poolId_(v.poolId_),
        copySetId_(v.copySetId_),
        partitionId_(v.partitionId_),
        idStart_(v.idStart_),
        idEnd_(v.idEnd_),
        idNext_(v.idNext_),
        txId_(v.txId_),
        status_(v.status_),
        inodeNum_(v.inodeNum_),
        dentryNum_(v.dentryNum_),
        fileType2InodeNum_(v.fileType2InodeNum_) {}

  Partition& operator=(const Partition& v) {
    if (&v == this) {
      return *this;
    }
    fsId_ = v.fsId_;
    poolId_ = v.poolId_;
    copySetId_ = v.copySetId_;
    partitionId_ = v.partitionId_;
    idStart_ = v.idStart_;
    idEnd_ = v.idEnd_;
    idNext_ = v.idNext_;
    txId_ = v.txId_;
    status_ = v.status_;
    inodeNum_ = v.inodeNum_;
    dentryNum_ = v.dentryNum_;
    fileType2InodeNum_ = v.fileType2InodeNum_;
    return *this;
  }

  explicit Partition(const pb::common::PartitionInfo& v) {
    fsId_ = v.fsid();
    poolId_ = v.poolid();
    copySetId_ = v.copysetid();
    partitionId_ = v.partitionid();
    idStart_ = v.start();
    idEnd_ = v.end();
    txId_ = v.txid();
    status_ = v.status();
    inodeNum_ = v.has_inodenum() ? v.inodenum() : 0;
    dentryNum_ = v.has_dentrynum() ? v.dentrynum() : 0;
    for (auto const& i : v.filetype2inodenum()) {
      fileType2InodeNum_.emplace(static_cast<FileType>(i.first), i.second);
    }
    idNext_ = v.has_nextid() ? v.nextid() : 0;
  }

  explicit operator pb::common::PartitionInfo() const {
    pb::common::PartitionInfo partition;
    partition.set_fsid(fsId_);
    partition.set_poolid(poolId_);
    partition.set_copysetid(copySetId_);
    partition.set_partitionid(partitionId_);
    partition.set_start(idStart_);
    partition.set_end(idEnd_);
    partition.set_txid(txId_);
    partition.set_status(status_);
    partition.set_inodenum(inodeNum_);
    partition.set_dentrynum(dentryNum_);
    auto partitionFileType2InodeNum = partition.mutable_filetype2inodenum();
    for (auto const& i : fileType2InodeNum_) {
      (*partitionFileType2InodeNum)[i.first] = i.second;
    }
    if (idNext_ != 0) {
      partition.set_nextid(idNext_);
    }
    return partition;
  }

  FsIdType GetFsId() const { return fsId_; }

  void SetFsId(FsIdType fsId) { fsId_ = fsId; }

  PoolIdType GetPoolId() const { return poolId_; }

  void SetPoolId(PoolIdType poolId) { poolId_ = poolId; }

  CopySetIdType GetCopySetId() const { return copySetId_; }

  void SetCopySetId(CopySetIdType copySetId) { copySetId_ = copySetId; }

  PartitionIdType GetPartitionId() const { return partitionId_; }

  void SetPartitionId(PartitionIdType partitionId) {
    partitionId_ = partitionId;
  }

  uint64_t GetIdStart() const { return idStart_; }

  void SetIdStart(uint64_t idStart) { idStart_ = idStart; }

  uint64_t GetIdEnd() const { return idEnd_; }

  void SetIdEnd(uint64_t idEnd) { idEnd_ = idEnd; }

  uint64_t GetIdNext() const { return idNext_; }

  void SetIdNext(uint64_t idNext) { idNext_ = idNext; }

  uint64_t GetTxId() const { return txId_; }

  void SetTxId(uint64_t txId) { txId_ = txId; }

  pb::common::PartitionStatus GetStatus() const { return status_; }

  void SetStatus(pb::common::PartitionStatus status) { status_ = status; }

  uint64_t GetInodeNum() const { return inodeNum_; }

  void SetInodeNum(uint64_t inodeNum) { inodeNum_ = inodeNum; }

  uint64_t GetDentryNum() const { return dentryNum_; }

  void SetDentryNum(uint64_t dentryNum) { dentryNum_ = dentryNum; }

  ::dingofs::utils::RWLock& GetRWLockRef() const { return mutex_; }

  bool SerializeToString(std::string* value) const;

  bool ParseFromString(const std::string& value);

  pb::common::PartitionInfo ToPartitionInfo();

  std::unordered_map<FileType, uint64_t> GetFileType2InodeNum() const {
    return fileType2InodeNum_;
  }

  void SetFileType2InodeNum(const std::unordered_map<FileType, uint64_t>& map) {
    fileType2InodeNum_ = map;
  }

  void InitFileType2InodeNum() {
    for (int i = pb::metaserver::FsFileType_MIN;
         i <= pb::metaserver::FsFileType_MAX; ++i) {
      fileType2InodeNum_.emplace(static_cast<FileType>(i), 0);
    }
  }

 private:
  FsIdType fsId_;
  PoolIdType poolId_;
  CopySetIdType copySetId_;
  PartitionIdType partitionId_;
  uint64_t idStart_;
  uint64_t idEnd_;
  uint64_t idNext_;
  uint64_t txId_;
  pb::common::PartitionStatus status_;
  uint64_t inodeNum_;
  uint64_t dentryNum_;
  std::unordered_map<FileType, uint64_t> fileType2InodeNum_;
  mutable ::dingofs::utils::RWLock mutex_;
};

class MemcacheServer {
 public:
  MemcacheServer() : port_(0) {}
  explicit MemcacheServer(const pb::mds::topology::MemcacheServerInfo& info)
      : ip_(info.ip()), port_(info.port()) {}
  explicit MemcacheServer(const std::string&& ip, uint32_t port)
      : ip_(ip), port_(port) {}

  MemcacheServer& operator=(const pb::mds::topology::MemcacheServerInfo& info) {
    ip_ = info.ip();
    port_ = info.port();
    return *this;
  }

  operator pb::mds::topology::MemcacheServerInfo() const {
    pb::mds::topology::MemcacheServerInfo info;
    info.set_ip(ip_);
    info.set_port(port_);
    return info;
  }

  bool operator==(const pb::mds::topology::MemcacheServerInfo& server) const {
    return ip_ == server.ip() && port_ == server.port();
  }

  std::string GetIp() const { return ip_; }

  uint32_t GetPort() const { return port_; }

 private:
  std::string ip_;
  uint32_t port_;
};

class MemcacheCluster {
 public:
  MemcacheCluster() : id_(UNINITIALIZE_ID) {}
  explicit MemcacheCluster(const pb::mds::topology::MemcacheClusterInfo& info)
      : id_(info.clusterid()) {
    for (auto const& server : info.servers()) {
      servers_.emplace_back(server);
    }
  }

  MemcacheCluster(MetaServerIdType id, std::list<MemcacheServer>&& servers)
      : id_(id), servers_(servers) {}

  MemcacheCluster(MetaServerIdType id, const std::list<MemcacheServer>& servers)
      : id_(id), servers_(servers) {}

  MemcacheCluster& operator=(
      const pb::mds::topology::MemcacheClusterInfo& info) {
    id_ = info.clusterid();
    for (auto const& server : info.servers()) {
      servers_.emplace_back(server);
    }
    return *this;
  }

  operator pb::mds::topology::MemcacheClusterInfo() const {
    pb::mds::topology::MemcacheClusterInfo info;
    info.set_clusterid(id_);
    for (auto const& server : servers_) {
      (*info.add_servers()) =
          static_cast<pb::mds::topology::MemcacheServerInfo>(server);
    }
    return info;
  }

  bool operator==(const MemcacheCluster& rhs) const {
    if (rhs.id_ != id_ || rhs.servers_.size() != servers_.size()) {
      return false;
    }
    for (auto const& server : servers_) {
      if (std::find(rhs.servers_.cbegin(), rhs.servers_.cend(), server) ==
          rhs.servers_.cend()) {
        return false;
      }
    }
    return true;
  }

  std::list<MemcacheServer> GetServers() const { return servers_; }

  MetaServerIdType GetId() const { return id_; }

  bool ParseFromString(const std::string& value);
  bool SerializeToString(std::string* value) const;

  void SetId(MetaServerIdType id) { id_ = id; }

 private:
  MetaServerIdType id_;
  std::list<MemcacheServer> servers_;
};

}  // namespace topology
}  // namespace mds
}  // namespace dingofs

#endif  // DINGOFS_SRC_MDS_TOPOLOGY_TOPOLOGY_ITEM_H_
