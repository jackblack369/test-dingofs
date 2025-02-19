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

#ifndef DINGOFS_SRC_METASERVER_REGISTER_H_
#define DINGOFS_SRC_METASERVER_REGISTER_H_

#include <string>
#include <vector>

#include "dingofs/metaserver.pb.h"

namespace dingofs {
namespace metaserver {
const uint32_t CURRENT_METADATA_VERSION = 0x01;

struct RegisterOptions {
  std::string mdsListenAddr;
  std::string metaserverInternalIp;
  std::string metaserverExternalIp;
  uint32_t metaserverInternalPort;
  uint32_t metaserverExternalPort;
  int registerRetries;
  int registerTimeout;
};

class Register {
 public:
  explicit Register(const RegisterOptions& ops);
  ~Register() {}

  int RegisterToMDS(pb::metaserver::MetaServerMetadata* metadata);

 private:
  RegisterOptions ops_;

  std::vector<std::string> mdsEps_;
  int inServiceIndex_;
};
}  // namespace metaserver
}  // namespace dingofs

#endif  // DINGOFS_SRC_METASERVER_REGISTER_H_
