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
 * Created Date: 2021-09-16
 * Author: chenwei
 */

#include "mds/heartbeat/heartbeat_service.h"

#include <memory>

namespace dingofs {
namespace mds {
namespace heartbeat {
HeartbeatServiceImpl::HeartbeatServiceImpl(
    std::shared_ptr<HeartbeatManager> heartbeat_manager) {
  this->heartbeatManager_ = heartbeat_manager;
}

void HeartbeatServiceImpl::MetaServerHeartbeat(
    ::google::protobuf::RpcController* controller,
    const ::dingofs::mds::heartbeat::MetaServerHeartbeatRequest* request,
    ::dingofs::mds::heartbeat::MetaServerHeartbeatResponse* response,
    ::google::protobuf::Closure* done) {
  (void)controller;
  brpc::ClosureGuard done_guard(done);
  heartbeatManager_->MetaServerHeartbeat(*request, response);
}
}  // namespace heartbeat
}  // namespace mds
}  // namespace dingofs
