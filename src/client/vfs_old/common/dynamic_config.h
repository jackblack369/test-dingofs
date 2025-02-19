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
 * Created Date: 2024-09-07
 * Author: Jingli Chen (Wine93)
 */

#ifndef DINGOFS_SRC_CLIENT_VFS_OLD_COMMON_DYNAMIC_CONFIG_H_
#define DINGOFS_SRC_CLIENT_VFS_OLD_COMMON_DYNAMIC_CONFIG_H_

#include <gflags/gflags.h>

namespace dingofs {
namespace client {
namespace common {

#define USING_FLAG(name) using ::dingofs::client::common::FLAGS_##name;

/**
 * You can modify the config on the fly, e.g.
 *
 * curl -s http://127.0.0.1:9000/flags/block_cache_logging?setvalue=true
 */

// ----- related stat or quota config -----
// thread num or bthread num
DECLARE_uint32(stat_timer_thread_num);
DECLARE_uint32(fs_usage_flush_interval_second);
DECLARE_uint32(flush_quota_interval_second);
DECLARE_uint32(load_quota_interval_second);

// push metrics interval
DECLARE_uint32(push_metric_interval_millsecond);

// fuse client
DECLARE_uint32(fuse_read_max_retry_s3_not_exist);

DECLARE_bool(useFakeS3);

// ----- related fuse client -----
DECLARE_bool(enableCto);
DECLARE_bool(supportKVcache);

DECLARE_uint64(fuseClientAvgWriteIops);
DECLARE_uint64(fuseClientBurstWriteIops);
DECLARE_uint64(fuseClientBurstWriteIopsSecs);

DECLARE_uint64(fuseClientAvgWriteBytes);
DECLARE_uint64(fuseClientBurstWriteBytes);
DECLARE_uint64(fuseClientBurstWriteBytesSecs);

DECLARE_uint64(fuseClientAvgReadIops);
DECLARE_uint64(fuseClientBurstReadIops);
DECLARE_uint64(fuseClientBurstReadIopsSecs);

DECLARE_uint64(fuseClientAvgReadBytes);
DECLARE_uint64(fuseClientBurstReadBytes);
DECLARE_uint64(fuseClientBurstReadBytesSecs);

}  // namespace common
}  // namespace client
}  // namespace dingofs

namespace brpc {
DECLARE_int32(defer_close_second);
DECLARE_int32(health_check_interval);
}  // namespace brpc

#endif  // DINGOFS_SRC_CLIENT_VFS_OLD_COMMON_DYNAMIC_CONFIG_H_
