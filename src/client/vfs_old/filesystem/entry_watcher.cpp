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
 * Created Date: 2024-08-29
 * Author: Jingli Chen (Wine93)
 */

#include "client/vfs_old/filesystem/entry_watcher.h"

#include "dingofs/metaserver.pb.h"
#include "base/filepath/filepath.h"
#include "base/string/string.h"
#include "client/vfs_old/filesystem/utils.h"
#include "glog/logging.h"

namespace dingofs {
namespace client {
namespace filesystem {

using base::filepath::HasSuffix;
using base::string::StrSplit;
using utils::LRUCache;
using utils::ReadLockGuard;
using utils::RWLock;
using utils::WriteLockGuard;

using pb::metaserver::InodeAttr;

EntryWatcher::EntryWatcher(const std::string& nocto_suffix) {
  nocto_ = std::make_unique<LRUType>(65536);

  if (nocto_suffix.empty()) {
    return;
  }

  std::vector<std::string> suffixs = StrSplit(nocto_suffix, ":");
  for (const auto& suffix : suffixs) {
    VLOG(3) << "nocto_suffix " << nocto_suffix << ", split suffix " << suffix;
    if (!suffix.empty()) {
      suffixs_.push_back(suffix);
    }
  }
}

void EntryWatcher::Remeber(const InodeAttr& attr, const std::string& filename) {
  if (!IsS3File(attr)) {
    return;
  }

  for (const auto& suffix : suffixs_) {
    if (HasSuffix(filename, suffix)) {
      WriteLockGuard lk(rwlock_);
      nocto_->Put(attr.inodeid(), true);
      return;
    }
  }
}

void EntryWatcher::Forget(Ino ino) {
  WriteLockGuard lk(rwlock_);
  nocto_->Remove(ino);
}

bool EntryWatcher::ShouldWriteback(Ino ino) {
  ReadLockGuard lk(rwlock_);
  bool ignore;
  return nocto_->Get(ino, &ignore);
}

}  // namespace filesystem
}  // namespace client
}  // namespace dingofs
