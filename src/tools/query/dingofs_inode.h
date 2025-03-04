/*
 *  Copyright (c) 2022 NetEase Inc.
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
 * Created Date: 2022-04-29
 * Author: chengyi01
 */
#ifndef DINGOFS_SRC_TOOLS_QUERY_DINGOFS_INODE_H_
#define DINGOFS_SRC_TOOLS_QUERY_DINGOFS_INODE_H_

#include <brpc/channel.h>
#include <gflags/gflags.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "dingofs/metaserver.pb.h"
#include "tools/dingofs_tool.h"
#include "tools/dingofs_tool_define.h"
#include "tools/query/dingofs_inode_s3infomap.h"

namespace dingofs {
namespace tools {
namespace query {

using InodeBaseInfo = pb::metaserver::Inode;

class InodeTool
    : public DingofsToolRpc<pb::metaserver::GetInodeRequest,
                            pb::metaserver::GetInodeResponse,
                            pb::metaserver::MetaServerService_Stub> {
 public:
  explicit InodeTool(const std::string& cmd = kNoInvokeCmd, bool show = true)
      : DingofsToolRpc(cmd) {
    show_ = show;
  }

  void PrintHelp() override;
  int Init() override;
  std::unordered_map<InodeBase, std::vector<InodeBaseInfo>, HashInodeBase,
                     KeyEuqalInodeBase>
  GetInode2InodeBaseInfoList() {
    return inode2InodeBaseInfoList_;
  }

 protected:
  void AddUpdateFlags() override;
  bool AfterSendRequestToHost(const std::string& host) override;
  bool CheckRequiredFlagDefault() override;

  /**
   * @brief
   *
   * @param inode
   * @param list
   * @return true : success
   * @return false : inode2InodeBaseInfoList_[inode].second.size() > 0
   * means  found two inode in the fs
   */
  bool UpdateInode2InodeBaseInfoList_(const InodeBase& inode,
                                      const InodeBaseInfo& list);

 protected:
  std::unordered_map<InodeBase, std::vector<InodeBaseInfo>, HashInodeBase,
                     KeyEuqalInodeBase>
      inode2InodeBaseInfoList_;
};

}  // namespace query
}  // namespace tools
}  // namespace dingofs

#endif  // DINGOFS_SRC_TOOLS_QUERY_DINGOFS_INODE_H_
