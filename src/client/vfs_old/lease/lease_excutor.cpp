/*
 *  Copyright (c) 2022 NetEase Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License"){}
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
 * Created Date: Tue Mar 29 2022
 * Author: lixiaocui
 */

#include "client/vfs_old/lease/lease_excutor.h"

#include <vector>

#include "brpc/periodic_task.h"
#include "glog/logging.h"

namespace dingofs {
namespace client {

using pb::mds::topology::PartitionTxId;

LeaseExecutor::~LeaseExecutor() {
  if (task_) {
    task_->Stop();
    task_->WaitTaskExit();
  }
}

bool LeaseExecutor::Start() {
  if (opt_.leaseTimeUs <= 0 || opt_.refreshTimesPerLease <= 0) {
    LOG(ERROR) << "LeaseExecutor start fail. Invalid param in leaseopt, "
                  "leaseTimeUs = "
               << opt_.leaseTimeUs
               << ", refreshTimePerLease = " << opt_.refreshTimesPerLease;
    return false;
  }

  uint32_t interval = opt_.leaseTimeUs / opt_.refreshTimesPerLease;
  task_.reset(new (std::nothrow) RefreshSessionTask(this, interval));
  if (task_ == nullptr) {
    LOG(ERROR) << "LeaseExecutor allocate refresh session task fail";
    return false;
  }

  timespec abstime = butil::microseconds_from_now(interval);
  brpc::PeriodicTaskManager::StartTaskAt(task_.get(), abstime);

  LOG(INFO) << "LeaseExecutor for client started, lease interval is "
            << interval << "us";
  return true;
}

void LeaseExecutor::Stop() {
  if (task_ != nullptr) {
    task_->Stop();

    LOG(INFO) << "LeaseExecutor for client stop";
  }
}

bool LeaseExecutor::RefreshLease() {
  // get partition txid list
  std::vector<PartitionTxId> txIds;
  metaCache_->GetAllTxIds(&txIds);

  // refresh from mds
  std::vector<PartitionTxId> latestTxIdList;
  pb::mds::FSStatusCode ret = mdsCli_->RefreshSession(
      txIds, &latestTxIdList, fsName_, mountpoint_, enableSumInDir_);
  if (ret != pb::mds::FSStatusCode::OK) {
    LOG(ERROR) << "LeaseExecutor refresh session fail, ret = " << ret
               << ", errorName = " << FSStatusCode_Name(ret);
    return true;
  }

  // update to metacache
  std::for_each(latestTxIdList.begin(), latestTxIdList.end(),
                [&](const PartitionTxId& item) {
                  metaCache_->SetTxId(item.partitionid(), item.txid());
                });
  return true;
}

}  // namespace client
}  // namespace dingofs
