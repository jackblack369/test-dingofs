/*
 * Copyright (c) 2024 dingodb.com, Inc. All Rights Reserved
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Project: DingoFS
 * Created Date: 2024-10-31
 * Author: Jingli Chen (Wine93)
 */

#ifndef DINGOFS_SRC_METASERVER_SUPERPARTITION_ACCESS_LOG_H_
#define DINGOFS_SRC_METASERVER_SUPERPARTITION_ACCESS_LOG_H_

#include <butil/time.h>

#include <functional>
#include <string>

namespace dingofs {
namespace metaserver {
namespace superpartition {

bool InitAccessLog(const std::string& prefix);

void ShutdownAccessLog();

class LogGuard {
 public:
  using MessageHandler = std::function<std::string()>;

 public:
  LogGuard(MessageHandler handler);

  ~LogGuard();

 private:
  bool enable_;
  MessageHandler handler_;
  butil::Timer timer_;
};

}  // namespace superpartition
}  // namespace metaserver
}  // namespace dingofs

#endif  // DINGOFS_SRC_METASERVER_SUPERPARTITION_ACCESS_LOG_H_
