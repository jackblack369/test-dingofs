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
 * Created Date: 2021-09-14
 * Author: chengyi01
 */
#ifndef DINGOFS_SRC_TOOLS_DINGOFS_TOOL_H_
#define DINGOFS_SRC_TOOLS_DINGOFS_TOOL_H_

#include <brpc/channel.h>
#include <bthread/bthread.h>
#include <google/protobuf/message.h>
#include <google/protobuf/service.h>
#include <json/json.h>

#include <algorithm>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <queue>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "common/rpc_stream.h"
#include "tools/dingofs_tool_define.h"
#include "tools/dingofs_tool_metric.h"
#include "utils/configuration.h"

DECLARE_string(confPath);
DECLARE_uint32(rpcTimeoutMs);
DECLARE_uint32(rpcRetryTimes);
DECLARE_uint32(rpcStreamIdleTimeoutMs);
DECLARE_uint32(rpcRetryIntervalUs);

namespace dingofs {
namespace tools {

using ::dingofs::common::StreamClient;
using ::dingofs::common::StreamConnection;
using ::dingofs::common::StreamOptions;
using ::dingofs::common::StreamStatus;

class DingofsTool {
 public:
  DingofsTool() {}
  DingofsTool(const std::string& command,
              const std::string& programe = kProgrameName, bool show = true)
      : command_(command), programe_(programe), show_(show) {}

  virtual ~DingofsTool() {}

  virtual void PrintHelp();

  virtual int Run();

  /**
   * @brief configure the environment for the command
   *
   * @details
   */
  virtual int Init() = 0;

  /**
   * @brief return the result of executing the command
   *
   * @return int
   * 0: success
   * !0: fail
   * @details
   */
  virtual int RunCommand() = 0;

  /**
   * @brief print the non-essential error that occurred during execution
   *
   * @details
   */
  virtual void PrintError();

 protected:
  std::string command_;
  std::string programe_;
  std::stringstream errorOutput_;
  bool show_;  // Control whether the command line is output
};

/**
 * @brief this base class used for dingofs tool with rpc
 *
 * @tparam ChannelT
 * @tparam ControllerT
 * @tparam RequestT
 * @tparam ResponseT
 * @tparam ServiceT
 * @details
 * you can take umountfs as example
 */
template <typename RequestT, typename ResponseT, typename ServiceT,
          typename ChannelT = brpc::Channel,
          typename ControllerT = brpc::Controller>
class DingofsToolRpc : public DingofsTool {
 public:
  DingofsToolRpc(const std::string& command,
                 const std::string& programe = kProgrameName, bool show = true)
      : DingofsTool(command, programe, show) {}

  int Init(const std::shared_ptr<ChannelT>& channel,
           const std::shared_ptr<ControllerT>& controller,
           const std::queue<RequestT>& requestQueue,
           const std::shared_ptr<ResponseT>& response,
           const std::shared_ptr<ServiceT>& service_stub,
           const std::function<void(ControllerT*, RequestT*, ResponseT*)>&
               service_stub_func,
           const std::shared_ptr<StreamClient>& streamClient) {
    channel_ = channel;
    controller_ = controller;
    requestQueue_ = requestQueue;
    response_ = response;
    service_stub_ = service_stub;
    service_stub_func_ = service_stub_func;
    streamClient_ = streamClient;
    InitHostsAddr();
    return 0;
  }

  virtual int Init() {
    channel_ = std::make_shared<ChannelT>();
    controller_ = std::make_shared<ControllerT>();
    response_ = std::make_shared<ResponseT>();
    service_stub_ = std::make_shared<ServiceT>(channel_.get());
    streamClient_ = std::make_shared<StreamClient>();

    // add need update FlagInfos
    AddUpdateFlags();
    UpdateFlags();
    InitHostsAddr();
    if (CheckRequiredFlagDefault()) {
      return -1;
    }
    return 0;
  }

  virtual void SetRequestQueue(const std::queue<RequestT>& requestQueue) {
    requestQueue_ = requestQueue;
  }
  virtual void AddRequest(const RequestT& request) {
    requestQueue_.push(request);
  }

  virtual const std::shared_ptr<ResponseT>& GetResponse() { return response_; }

  virtual int RunCommand() {
    int ret = 0;
    while (!requestQueue_.empty()) {
      if (!SendRequestToServices()) {
        ret = -1;
      }

      requestQueue_.pop();
    }

    return ret;
  }

  virtual void InitHostsAddr() {}

  /**
   * @brief Check if required flag is default or not
   *
   * @return true: flag is default
   * @return false: flag is not default
   * @details
   * Check whether the required flag is the default
   * such as the parameters of some query commands
   */
  virtual bool CheckRequiredFlagDefault() { return false; }

 protected:
  /**
   * @brief send request to host in hostsAddr_
   *
   * @return true
   * @return false
   * @details
   * as long as one succeeds, it returns true and ends sending
   */
  virtual bool SendRequestToServices() {
    uint32_t failHostNumber = 0;
    bool ret = false;
    for (const std::string& host : hostsAddr_) {
      brpc::ChannelOptions channelOpt;
      if (isStreaming_) {
        // set stream rpc client
        channelOpt.connection_group = "streaming";
        StreamOptions streamOpt(FLAGS_rpcStreamIdleTimeoutMs);
        connection_ = streamClient_->Connect(controller_.get(),
                                             receiveCallback_, streamOpt);
        if (nullptr == connection_ || nullptr == receiveCallback_) {
          errorOutput_ << "Stream connect " << host << " failed\n";
          ++failHostNumber;
          continue;
        }
      }
      if (channel_->Init(host.c_str(), &channelOpt) != 0) {
        errorOutput_ << "fail init channel to host: " << host << std::endl;
        ++failHostNumber;
        continue;
      }
      uint32_t i = 0;
      bool changeServer = false;
      for (i = 0; i <= FLAGS_rpcRetryTimes; ++i) {
        controller_->Reset();
        SetController();
        // if service_stub_func_ does not assign a value
        // it will crash in there
        service_stub_func_(controller_.get(), &requestQueue_.front(),
                           response_.get());
        if (!controller_->Failed()) {
          // send success
          break;
        }
        int32_t retCode = controller_->ErrorCode();
        if (retCode == EHOSTDOWN || retCode == ECONNRESET ||
            retCode == ECONNREFUSED || retCode == brpc::ELOGOFF) {
          // no need to retry
          changeServer = true;
          bthread_usleep(FLAGS_rpcRetryIntervalUs);
          break;
        }
        bthread_usleep(FLAGS_rpcRetryIntervalUs);
      }
      if (i > FLAGS_rpcRetryTimes || changeServer) {
        ++failHostNumber;
      }
      if (isStreaming_) {
        auto status = connection_->WaitAllDataReceived();
        if (status != StreamStatus::STREAM_OK) {
          errorOutput_ << "Receive stream data from " << host
                       << " failed , status=" << status << std::endl;
        }
      }
      if (AfterSendRequestToHost(host) == true) {
        controller_->Reset();
        ret = true;
        break;
      }
      controller_->Reset();
      if (isStreaming_ && connection_ != nullptr) {
        streamClient_->Close(connection_);
        connection_ = nullptr;
      }
      SetController();
    }
    if (hostsAddr_.size() != failHostNumber) {
      errorOutput_.str("");
    }

    return ret;
  }

  virtual void SetController() {
    controller_->set_timeout_ms(FLAGS_rpcTimeoutMs);
    controller_->set_max_retry(0);
  }

  void AddUpdateFlagsFunc(
      const std::function<void(dingofs::utils::Configuration*,
                               google::CommandLineFlagInfo*)>& func) {
    updateFlagsFunc_.push_back(func);
  }

  virtual void UpdateFlags() {
    dingofs::utils::Configuration conf;
    conf.SetConfigPath(FLAGS_confPath);
    if (!conf.LoadConfig()) {
      std::cerr << "load configure file " << FLAGS_confPath << " failed!"
                << std::endl;
    }
    google::CommandLineFlagInfo info;

    for (auto& i : updateFlagsFunc_) {
      i(&conf, &info);
    }
  }

  /**
   * @brief add AddUpdateFlagsFunc in Subclass
   *
   * @details
   * use AddUpdateFlagsFunc to add UpdateFlagsFunc into updateFlagsFunc_;
   * add this function will be called in UpdateFlags;
   * this function should be called before UpdateFlags (like Init()).
   */
  virtual void AddUpdateFlags() = 0;

  /**
   * @brief deal with response info, include output err info
   *
   * @param host
   * @return true: send one request success
   * @return false send one request failed
   * @details
   */
  virtual bool AfterSendRequestToHost(const std::string& host) = 0;

  /**
   * @brief
   *
   * @details
   * If necessary, you can override RunCommand in a subclass:
   *      DingofsToolRpc::RunCommand();
   *      RemoveFailHostFromHostAddr();
   * Add the fail host in AfterSendRequestToHost:
   *      failHostsAddr_.push_back();
   */
  void RemoveFailHostFromHostAddr() {
    for (auto const& i : failHostsAddr_) {
      hostsAddr_.erase(std::remove_if(hostsAddr_.begin(), hostsAddr_.end(),
                                      [i](decltype(i) j) { return i == j; }),
                       hostsAddr_.end());
    }
  }

  void SetStreamingRpc(bool isStreaming) { isStreaming_ = isStreaming; }

 protected:
  /**
   * @brief save the host who will be sended request
   * like ip:port
   *
   * @details
   */
  std::vector<std::string> hostsAddr_;
  /**
   * @brief The hosts that failed to send the request
   *
   * @details
   */
  std::vector<std::string> failHostsAddr_;
  std::shared_ptr<ChannelT> channel_;
  std::shared_ptr<ControllerT> controller_;
  std::queue<RequestT> requestQueue_;  // should be defined in Init()
  std::shared_ptr<ResponseT> response_;
  std::shared_ptr<ServiceT> service_stub_;
  /**
   * @brief this functor will called in SendRequestToService
   * Generally need to be assigned to the service_stub_'s request
   * If service_stub_func_ does not assign a value
   * it will crash in SendRequestToService
   *
   * @details
   * it is core function of this class
   * make sure uint test cover SendRequestToServices
   */
  std::function<void(ControllerT*, RequestT*, ResponseT*)> service_stub_func_ =
      nullptr;
  /**
   * @brief save the functor which defined in dingofs_tool_define.h
   *
   * @details
   */
  std::vector<std::function<void(dingofs::utils::Configuration*,
                                 google::CommandLineFlagInfo*)>>
      updateFlagsFunc_;
  /**
   * @brief whether to use stream rpc api
   */
  bool isStreaming_ = false;
  /**
   * @brief rpc streaming client for too large data
   */
  std::shared_ptr<StreamClient> streamClient_;
  /**
   * @brief rpc stream client callback function for processing received data
   *
   */
  std::function<bool(butil::IOBuf* buffer)> receiveCallback_ = nullptr;

  std::shared_ptr<StreamConnection> connection_;
};

class DingofsToolMetric : public DingofsTool {
 public:
  explicit DingofsToolMetric(const std::string& command,
                             const std::string& programe = kProgrameName,
                             bool show = true)
      : DingofsTool(command, programe, show) {
    metricClient_ = std::make_shared<MetricClient>();
  }

  int Init(const std::shared_ptr<MetricClient>& metricClient);

  virtual void PrintHelp();

  virtual void InitHostsAddr() {}

 protected:
  void AddUpdateFlagsFunc(
      const std::function<void(dingofs::utils::Configuration*,
                               google::CommandLineFlagInfo*)>& func);

  virtual int RunCommand();

  virtual int Init();

  /**
   * @brief add AddUpdateFlagsFunc in Subclass
   *
   * @details
   * use AddUpdateFlagsFunc to add UpdateFlagsFunc into updateFlagsFunc_;
   * add this function will be called in UpdateFlags;
   * this function should be called before UpdateFlags (like Init()).
   *
   */
  virtual void AddUpdateFlags();

  virtual void UpdateFlags();

  virtual void AfterGetMetric(const std::string mdsAddr,
                              const std::string& subUri,
                              const std::string& Value,
                              const MetricStatusCode& statusCode) = 0;

  /**
   * @brief
   *
   * @return int
   * 0: no error
   * -1: error
   * @details
   */
  virtual int ProcessMetrics() = 0;

  void AddAddr2Suburi(const std::pair<std::string, std::string>& addrSubUri);

 protected:
  std::shared_ptr<MetricClient> metricClient_;
  /**
   * @brief get metricName from addr
   *
   * @details
   * first: addr  second: MetricName
   */
  std::vector<std::pair<std::string, std::string>> addr2SubUri;
  /**
   * @brief save the functor which defined in dingofs_tool_define.h
   *
   * @details
   */
  std::vector<std::function<void(dingofs::utils::Configuration*,
                                 google::CommandLineFlagInfo*)>>
      updateFlagsFunc_;
};
}  // namespace tools
}  // namespace dingofs

#endif  // DINGOFS_SRC_TOOLS_DINGOFS_TOOL_H_
