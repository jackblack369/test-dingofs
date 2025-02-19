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
 * Created Date: 2023-03-17
 * Author: Jingli Chen (Wine93)
 */

#include <absl/strings/str_format.h>
#include <butil/time.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/spdlog.h>
#include <unistd.h>

#include <string>

#include "client/common/dynamic_config.h"

#ifndef DINGOFS_CLIENT_ACCESS_LOG_H_
#define DINGOFS_CLIENT_ACCESS_LOG_H_

namespace dingofs {
namespace client {

static std::shared_ptr<spdlog::logger> logger;

static bool InitAccessLog(const std::string& prefix) {
  std::string filename = absl::StrFormat("%s/access_%d.log", prefix, getpid());
  logger = spdlog::daily_logger_mt("fuse_access", filename, 0, 0);
  spdlog::flush_every(std::chrono::seconds(1));
  return true;
}

struct AccessLogGuard {
  using MessageHandler = std::function<std::string()>;

  explicit AccessLogGuard(MessageHandler handler)
      : enable(common::FLAGS_access_logging), handler(handler) {
    if (!enable) {
      return;
    }

    timer.start();
  }

  ~AccessLogGuard() {
    if (!enable) {
      return;
    }

    timer.stop();
    logger->info("{0} <{1:.6f}>", handler(), timer.u_elapsed() / 1e6);
  }

  bool enable;
  MessageHandler handler;
  butil::Timer timer;
};

}  // namespace client
}  // namespace dingofs

#endif  // DINGOFS_CLIENT_ACCESS_LOG_H_
