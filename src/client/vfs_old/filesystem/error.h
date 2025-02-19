/*
 *  Copyright (c) 2023 NetEase Inc.
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
 * Project: Dingofs
 * Created Date: 2023-03-06
 * Author: Jingli Chen (Wine93)
 */

#include <iostream>
#include <string>

#include "dingofs/metaserver.pb.h"
#include "client/common/status.h"

#ifndef DINGOFS_SRC_CLIENT_FILESYSTEM_ERROR_H_
#define DINGOFS_SRC_CLIENT_FILESYSTEM_ERROR_H_

namespace dingofs {
namespace client {
namespace filesystem {

enum class DINGOFS_ERROR {
  OK = 0,
  INTERNAL = -1,
  UNKNOWN = -2,
  EXISTS = -3,
  NOTEXIST = -4,
  NO_SPACE = -5,
  BAD_FD = -6,
  INVALIDPARAM = -7,
  NOPERMISSION = -8,
  NOTEMPTY = -9,
  NOFLUSH = -10,
  NOTSUPPORT = -11,
  NAMETOOLONG = -12,
  MOUNT_POINT_EXIST = -13,
  MOUNT_FAILED = -14,
  OUT_OF_RANGE = -15,
  NODATA = -16,
  IO_ERROR = -17,
  CACHETOOSMALL = -18,
  STALE = -19,
  NOSYS = -20,
  NOPERMITTED = -21,
};

std::string StrErr(DINGOFS_ERROR code);

int SysErr(DINGOFS_ERROR code);

std::ostream& operator<<(std::ostream& os, DINGOFS_ERROR code);

DINGOFS_ERROR ToFSError(pb::metaserver::MetaStatusCode code);

Status DingofsErrorToStatus(DINGOFS_ERROR code);

}  // namespace filesystem

using ErrNo = filesystem::DINGOFS_ERROR;

}  // namespace client
}  // namespace dingofs

#endif  // DINGOFS_SRC_CLIENT_FILESYSTEM_ERROR_H_
