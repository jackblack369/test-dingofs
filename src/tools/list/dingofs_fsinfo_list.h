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
 * Created Date: 2021-10-31
 * Author: chengyi01
 */

#ifndef DINGOFS_SRC_TOOLS_LIST_DINGOFS_FSINFO_LIST_H_
#define DINGOFS_SRC_TOOLS_LIST_DINGOFS_FSINFO_LIST_H_

#include <brpc/channel.h>
#include <gflags/gflags.h>

#include <string>

#include "dingofs/mds.pb.h"
#include "tools/dingofs_tool.h"
#include "tools/dingofs_tool_define.h"
#include "utils/string_util.h"

namespace dingofs {
namespace tools {
namespace list {

class FsInfoListTool : public DingofsToolRpc<pb::mds::ListClusterFsInfoRequest,
                                             pb::mds::ListClusterFsInfoResponse,
                                             pb::mds::MdsService_Stub> {
 public:
  explicit FsInfoListTool(const std::string& cmd = kFsInfoListCmd,
                          bool show = true)
      : DingofsToolRpc(cmd) {
    show_ = show;
  }
  void PrintHelp() override;
  int Init() override;

 protected:
  void AddUpdateFlags() override;
  bool AfterSendRequestToHost(const std::string& host) override;
};
}  // namespace list
}  // namespace tools
}  // namespace dingofs

#endif  // DINGOFS_SRC_TOOLS_LIST_DINGOFS_FSINFO_LIST_H_
