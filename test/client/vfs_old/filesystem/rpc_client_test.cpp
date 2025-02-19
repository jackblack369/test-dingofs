
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
 * Created Date: 2023-04-03
 * Author: Jingli Chen (Wine93)
 */

#include <gtest/gtest.h>

#include "client/vfs_old/filesystem/utils.h"
#include "client/vfs_old/filesystem/helper/helper.h"

namespace dingofs {
namespace client {
namespace filesystem {

class RPCClientTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
};

TEST_F(RPCClientTest, GetAttr_Basic) {
  auto builder = RPCClientBuilder();
  auto rpc = builder.Build();

  // CASE 1: ok
  {
    EXPECT_CALL_RETURN_GetInodeAttr(*builder.GetInodeManager(),
                                    DINGOFS_ERROR::OK);

    InodeAttr attr;
    auto rc = rpc->GetAttr(100, &attr);
    ASSERT_EQ(rc, DINGOFS_ERROR::OK);
  }

  // CASE 2: inode not exist
  {
    EXPECT_CALL_RETURN_GetInodeAttr(*builder.GetInodeManager(),
                                    DINGOFS_ERROR::NOTEXIST);

    InodeAttr attr;
    auto rc = rpc->GetAttr(100, &attr);
    ASSERT_EQ(rc, DINGOFS_ERROR::NOTEXIST);
  }
}

TEST_F(RPCClientTest, Lookup_Basic) {
  auto builder = RPCClientBuilder();
  auto rpc = builder.Build();

  // CASE 1: ok
  {
    EXPECT_CALL_RETURN_GetDentry(*builder.GetDentryManager(),
                                 DINGOFS_ERROR::OK);
    EXPECT_CALL_RETURN_GetInodeAttr(*builder.GetInodeManager(),
                                    DINGOFS_ERROR::OK);

    EntryOut entryOut;
    auto rc = rpc->Lookup(1, "f1", &entryOut);
    ASSERT_EQ(rc, DINGOFS_ERROR::OK);
  }

  // CASE 2: dentry not exist
  {
    EXPECT_CALL_RETURN_GetDentry(*builder.GetDentryManager(),
                                 DINGOFS_ERROR::NOTEXIST);

    EntryOut entryOut;
    auto rc = rpc->Lookup(1, "f1", &entryOut);
    ASSERT_EQ(rc, DINGOFS_ERROR::NOTEXIST);
  }

  // CASE 3: inode not exist
  {
    EXPECT_CALL_RETURN_GetDentry(*builder.GetDentryManager(),
                                 DINGOFS_ERROR::OK);
    EXPECT_CALL_RETURN_GetInodeAttr(*builder.GetInodeManager(),
                                    DINGOFS_ERROR::NOTEXIST);

    EntryOut entryOut;
    auto rc = rpc->Lookup(1, "f1", &entryOut);
    ASSERT_EQ(rc, DINGOFS_ERROR::NOTEXIST);
  }
}

TEST_F(RPCClientTest, ReadDir_Basic) {
  auto builder = RPCClientBuilder();
  auto rpc = builder.Build();

  // CASE 1: ok
  {
    EXPECT_CALL_INVOKE_ListDentry(
        *builder.GetDentryManager(),
        [&](uint64_t parent, std::list<Dentry>* dentries, uint32_t limit,
            bool only, uint32_t nlink) -> DINGOFS_ERROR {
          dentries->push_back(MkDentry(1, "test"));
          return DINGOFS_ERROR::OK;
        });
    EXPECT_CALL_INVOKE_BatchGetInodeAttrAsync(
        *builder.GetInodeManager(),
        [&](uint64_t parentId, std::set<uint64_t>* inos,
            std::map<uint64_t, InodeAttr>* attrs) -> DINGOFS_ERROR {
          for (const auto& ino : *inos) {
            auto attr = MkAttr(ino, AttrOption().mtime(123, ino));
            attrs->emplace(ino, attr);
          }
          return DINGOFS_ERROR::OK;
        });

    DirEntry dirEntry;
    auto entries = std::make_shared<DirEntryList>();
    auto rc = rpc->ReadDir(100, &entries);
    ASSERT_EQ(rc, DINGOFS_ERROR::OK);
    ASSERT_EQ(entries->Size(), 1);
    ASSERT_TRUE(entries->Get(1, &dirEntry));
    ASSERT_EQ(dirEntry.ino, 1);
    ASSERT_EQ(dirEntry.name, "test");
  }

  // CASE 2: inode not exist
  {
    EXPECT_CALL_RETURN_GetInodeAttr(*builder.GetInodeManager(),
                                    DINGOFS_ERROR::NOTEXIST);

    InodeAttr attr;
    auto rc = rpc->GetAttr(100, &attr);
    ASSERT_EQ(rc, DINGOFS_ERROR::NOTEXIST);
  }
}

TEST_F(RPCClientTest, Open_Basic) {
  auto builder = RPCClientBuilder();
  auto rpc = builder.Build();

  // CASE 1: ok
  {
    EXPECT_CALL_RETURN_GetInode(*builder.GetInodeManager(), DINGOFS_ERROR::OK);

    auto inode = MkInode(100);
    auto rc = rpc->Open(100, &inode);
    ASSERT_EQ(rc, DINGOFS_ERROR::OK);
  }

  // CASE 2: inode not exist
  {
    EXPECT_CALL_RETURN_GetInode(*builder.GetInodeManager(),
                                DINGOFS_ERROR::NOTEXIST);

    auto inode = MkInode(100);
    auto rc = rpc->Open(100, &inode);
    ASSERT_EQ(rc, DINGOFS_ERROR::NOTEXIST);
  }
}

}  // namespace filesystem
}  // namespace client
}  // namespace dingofs
