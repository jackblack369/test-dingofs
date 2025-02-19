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
 * Created Date: 2021-05-19
 * Author: chenwei
 */

#include "metaserver/dentry_manager.h"

#include <glog/logging.h>

#include <sstream>

namespace dingofs {
namespace metaserver {

using pb::metaserver::Dentry;
using pb::metaserver::MetaStatusCode;

DentryManager::DentryManager(std::shared_ptr<DentryStorage> dentryStorage,
                             std::shared_ptr<TxManager> txManager)
    : dentryStorage_(dentryStorage), txManager_(txManager) {}

void DentryManager::Log4Dentry(const std::string& request,
                               const Dentry& dentry) {
  VLOG(9) << "Receive " << request << " request, dentry = ("
          << dentry.ShortDebugString() << ")";
}

void DentryManager::Log4Code(const std::string& request, MetaStatusCode rc) {
  auto succ =
      (rc == MetaStatusCode::OK || rc == MetaStatusCode::IDEMPOTENCE_OK ||
       (rc == MetaStatusCode::NOT_FOUND &&
        (request == "ListDentry" || request == "GetDentry")));
  std::ostringstream message;
  message << request << " " << (succ ? "success" : "fail")
          << ", retCode = " << MetaStatusCode_Name(rc);

  if (succ) {
    VLOG(6) << message.str();
  } else {
    LOG(ERROR) << message.str();
  }
}

MetaStatusCode DentryManager::CreateDentry(const Dentry& dentry) {
  Log4Dentry("CreateDentry", dentry);
  MetaStatusCode rc = dentryStorage_->Insert(dentry);
  Log4Code("CreateDentry", rc);
  return rc;
}

MetaStatusCode DentryManager::CreateDentry(const pb::metaserver::DentryVec& vec,
                                           bool merge) {
  VLOG(9) << "Receive CreateDentryVec request, dentryVec = ("
          << vec.ShortDebugString() << ")";
  MetaStatusCode rc = dentryStorage_->Insert(vec, merge);
  Log4Code("CreateDentryVec", rc);
  return rc;
}

MetaStatusCode DentryManager::DeleteDentry(const Dentry& dentry) {
  Log4Dentry("DeleteDentry", dentry);
  MetaStatusCode rc = dentryStorage_->Delete(dentry);
  Log4Code("DeleteDentry", rc);
  return rc;
}

MetaStatusCode DentryManager::GetDentry(Dentry* dentry) {
  Log4Dentry("GetDentry", *dentry);
  MetaStatusCode rc = dentryStorage_->Get(dentry);
  Log4Code("GetDentry", rc);
  return rc;
}

MetaStatusCode DentryManager::ListDentry(const Dentry& dentry,
                                         std::vector<Dentry>* dentrys,
                                         uint32_t limit, bool onlyDir) {
  Log4Dentry("ListDentry", dentry);
  MetaStatusCode rc = dentryStorage_->List(dentry, dentrys, limit, onlyDir);
  Log4Code("ListDentry", rc);
  return rc;
}

void DentryManager::ClearDentry() {
  dentryStorage_->Clear();
  LOG(INFO) << "ClearDentry ok";
}

MetaStatusCode DentryManager::HandleRenameTx(
    const std::vector<Dentry>& dentrys) {
  for (const auto& dentry : dentrys) {
    Log4Dentry("HandleRenameTx", dentry);
  }
  auto rc = txManager_->HandleRenameTx(dentrys);
  Log4Code("HandleRenameTx", rc);
  return rc;
}

}  // namespace metaserver
}  // namespace dingofs
