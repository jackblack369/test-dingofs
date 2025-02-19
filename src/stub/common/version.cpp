// Copyright (c) 2024 dingodb.com, Inc. All Rights Reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "stub/common/version.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

namespace dingofs {
namespace stub {
namespace common {

DEFINE_bool(show_version, true, "Print DingoStore version Flag");

DEFINE_string(git_commit_hash, GIT_VERSION, "current git commit version");
DEFINE_string(git_tag_name, GIT_TAG_NAME, "current dingo git tag version");
DEFINE_string(git_commit_user, GIT_COMMIT_USER,
              "current dingo git commit user");
DEFINE_string(git_commit_mail, GIT_COMMIT_MAIL,
              "current dingo git commit mail");
DEFINE_string(git_commit_time, GIT_COMMIT_TIME,
              "current dingo git commit time");
DEFINE_string(major_version, MAJOR_VERSION, "current dingo major version");
DEFINE_string(minor_version, MINOR_VERSION, "current dingo mino version");
DEFINE_string(dingofs_build_type, DINGOFS_BUILD_TYPE,
              "current dingofs build type");

void ShowVerion() {
  printf("DINGOFS VERSION:[%s-%s]\n", FLAGS_major_version.c_str(),
         FLAGS_minor_version.c_str());
  printf("DINGOFS GIT_TAG_VERSION:[%s]\n", FLAGS_git_tag_name.c_str());
  printf("DINGOFS GIT_COMMIT_HASH:[%s]\n", FLAGS_git_commit_hash.c_str());
  printf("DINGOFS BUILD_TYPE:[%s]\n", FLAGS_dingofs_build_type.c_str());
}

void LogVerion() {
  LOG(INFO) << "DINGOFS VERSION:[" << FLAGS_major_version << "-"
            << FLAGS_minor_version << "]";
  LOG(INFO) << "DINGOFS GIT_TAG_VERSION:[" << FLAGS_git_tag_name << "]";
  LOG(INFO) << "DINGOFS GIT_COMMIT_HASH:[" << FLAGS_git_commit_hash << "]";
  LOG(INFO) << "DINGOFS BUILD_TYPE:[" << FLAGS_dingofs_build_type << "]";
  LOG(INFO) << "PID: " << getpid();
}

}  // namespace common
}  // namespace stub
}  // namespace dingofs