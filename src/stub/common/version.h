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

#include <gflags/gflags_declare.h>

namespace dingofs {
namespace stub {
namespace common {

DECLARE_bool(show_version);

#ifndef GIT_VERSION
#define GIT_VERSION "unknown"
#endif

#ifndef MAJOR_VERSION
#define MAJOR_VERSION "v3.0.0"
#endif

#ifndef MINOR_VERSION
#define MINOR_VERSION "0"
#endif

#ifndef GIT_TAG_NAME
#define GIT_TAG_NAME "v3.0.0"
#endif

#ifndef GIT_COMMIT_USER
#define GIT_COMMIT_USER "unknown"
#endif

#ifndef GIT_COMMIT_MAIL
#define GIT_COMMIT_MAIL "unknown"
#endif

#ifndef GIT_COMMIT_TIME
#define GIT_COMMIT_TIME "unknown"
#endif

#ifndef DINGOFS_BUILD_TYPE
#define DINGOFS_BUILD_TYPE "unknown"
#endif

void ShowVerion();

void LogVerion();

}  // namespace common
}  // namespace stub
}  // namespace dingofs
