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
 * Created Date: 21-8-18
 * Author: huyao
 */
#ifndef DINGOFS_SRC_CLIENT_S3_CLIENT_S3_CACHE_MANAGER_H_
#define DINGOFS_SRC_CLIENT_S3_CLIENT_S3_CACHE_MANAGER_H_

#include <algorithm>
#include <cstring>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "dingofs/metaserver.pb.h"
#include "client/blockcache/cache_store.h"
#include "client/datastream/data_stream.h"
#include "client/vfs_old/filesystem/error.h"
#include "client/vfs_old/inode_wrapper.h"
#include "client/vfs_old/kvclient/kvclient_manager.h"
#include "utils/concurrent/concurrent.h"

namespace dingofs {
namespace client {

class S3ClientAdaptorImpl;
class ChunkCacheManager;
class FileCacheManager;
class FsCacheManager;
class DataCache;
using FileCacheManagerPtr = std::shared_ptr<FileCacheManager>;
using ChunkCacheManagerPtr = std::shared_ptr<ChunkCacheManager>;
using DataCachePtr = std::shared_ptr<DataCache>;
using WeakDataCachePtr = std::weak_ptr<DataCache>;

enum CacheType { Write = 1, Read = 2 };

struct ReadRequest {
  uint64_t index;
  uint64_t chunkPos;
  uint64_t len;
  uint64_t bufOffset;

  std::string DebugString() const {
    std::ostringstream os;
    os << "ReadRequest ( chunkIndex = " << index << ", chunkPos = " << chunkPos
       << ", len = " << len << ", bufOffset = " << bufOffset << " )";
    return os.str();
  }
};

struct S3ReadRequest {
  uint64_t chunkId;
  uint64_t offset;  // file offset
  uint64_t len;
  uint64_t objectOffset;  // s3 object's begin in the block
  uint64_t readOffset;    // read buf offset
  uint64_t fsId;
  uint64_t inodeId;
  uint64_t compaction;

  std::string DebugString() const {
    std::ostringstream os;
    os << "S3ReadRequest ( chunkId = " << chunkId << ", offset = " << offset
       << ", len = " << len << ", objectOffset = " << objectOffset
       << ", readOffset = " << readOffset << ", fsId = " << fsId
       << ", inodeId = " << inodeId << ", compaction = " << compaction << " )";
    return os.str();
  }
};

inline std::string S3ReadRequestVecDebugString(
    const std::vector<S3ReadRequest>& reqs) {
  std::ostringstream os;
  for_each(reqs.begin(), reqs.end(),
           [&](const S3ReadRequest& req) { os << req.DebugString() << " "; });
  return os.str();
}

struct ObjectChunkInfo {
  pb::metaserver::S3ChunkInfo s3ChunkInfo;
  uint64_t objectOffset;  // s3 object's begin in the block
};

struct PageData {
  uint64_t index;
  char* data;
};
using PageDataMap = std::map<uint64_t, PageData*>;

enum DataCacheStatus {
  Dirty = 1,
  Flush = 2,
};

class DataCache : public std::enable_shared_from_this<DataCache> {
 public:
  struct FlushBlock {
    FlushBlock(blockcache::BlockKey key,
               std::shared_ptr<aws::PutObjectAsyncContext> context)
        : key(key), context(context) {}

    blockcache::BlockKey key;
    std::shared_ptr<aws::PutObjectAsyncContext> context;
  };

 public:
  DataCache(S3ClientAdaptorImpl* s3ClientAdaptor,
            ChunkCacheManagerPtr chunkCacheManager, uint64_t chunkPos,
            uint64_t len, const char* data,
            std::shared_ptr<KVClientManager> kvClientManager);
  virtual ~DataCache() {
    auto iter = dataMap_.begin();
    for (; iter != dataMap_.end(); iter++) {
      auto pageIter = iter->second.begin();
      for (; pageIter != iter->second.end(); pageIter++) {
        datastream::DataStream::GetInstance().FreePage(pageIter->second->data);
        delete pageIter->second;
      }
    }
  }

  virtual void Write(uint64_t chunkPos, uint64_t len, const char* data,
                     const std::vector<DataCachePtr>& mergeDataCacheVer);
  virtual void Truncate(uint64_t size);
  uint64_t GetChunkPos() { return chunkPos_; }
  uint64_t GetLen() { return len_; }
  PageData* GetPageData(uint64_t blockIndex, uint64_t pageIndex) {
    PageDataMap& pdMap = dataMap_[blockIndex];
    if (pdMap.count(pageIndex)) {
      return pdMap[pageIndex];
    }
    return nullptr;
  }

  void ErasePageData(uint64_t blockIndex, uint64_t pageIndex) {
    dingofs::utils::LockGuard lg(mtx_);
    PageDataMap& pdMap = dataMap_[blockIndex];
    auto iter = pdMap.find(pageIndex);
    if (iter != pdMap.end()) {
      pdMap.erase(iter);
    }
    if (pdMap.empty()) {
      dataMap_.erase(blockIndex);
    }
  }

  uint64_t GetActualLen() { return actualLen_; }

  virtual DINGOFS_ERROR Flush(uint64_t inodeId, bool toS3 = false);
  void Release();
  bool IsDirty() {
    return status_.load(std::memory_order_acquire) == DataCacheStatus::Dirty;
  }
  virtual bool CanFlush(bool force);
  bool InReadCache() const {
    return inReadCache_.load(std::memory_order_acquire);
  }

  void SetReadCacheState(bool inCache) {
    inReadCache_.store(inCache, std::memory_order_release);
  }

  void Lock() { mtx_.lock(); }

  void UnLock() { mtx_.unlock(); }
  void CopyDataCacheToBuf(uint64_t offset, uint64_t len, char* data);
  void MergeDataCacheToDataCache(DataCachePtr mergeDataCache,
                                 uint64_t dataOffset, uint64_t len);

 private:
  void PrepareS3ChunkInfo(uint64_t chunkId, uint64_t offset, uint64_t len,
                          pb::metaserver::S3ChunkInfo* info);
  void CopyBufToDataCache(uint64_t dataCachePos, uint64_t len,
                          const char* data);
  void AddDataBefore(uint64_t len, const char* data);

  DINGOFS_ERROR PrepareFlushTasks(
      uint64_t inodeId, char* data, std::vector<FlushBlock>* s3Tasks,
      std::vector<std::shared_ptr<SetKVCacheTask>>* kvCacheTasks,
      uint64_t* chunkId, uint64_t* writeOffset);

  void FlushTaskExecute(
      bool to_s3, const std::vector<FlushBlock>& s3Tasks,
      const std::vector<std::shared_ptr<SetKVCacheTask>>& kvCacheTasks);

  S3ClientAdaptorImpl* s3ClientAdaptor_;
  ChunkCacheManagerPtr chunkCacheManager_;
  uint64_t chunkPos_;        // useful chunkPos
  uint64_t len_;             // useful len
  uint64_t actualChunkPos_;  // after alignment the actual chunkPos
  uint64_t actualLen_;       // after alignment the actual len
  dingofs::utils::Mutex mtx_;
  uint64_t createTime_;
  std::atomic<int> status_;
  std::atomic<bool> inReadCache_;
  std::map<uint64_t, PageDataMap> dataMap_;  // first is block index

  std::shared_ptr<KVClientManager> kvClientManager_;
};

class S3ReadResponse {
 public:
  explicit S3ReadResponse(char* data, uint64_t length)
      : data_(data), len_(length) {}

  char* GetDataBuf() { return data_; }

  uint64_t GetBufLen() { return len_; }

 private:
  char* data_;
  uint64_t len_;
};

class ChunkCacheManager
    : public std::enable_shared_from_this<ChunkCacheManager> {
 public:
  ChunkCacheManager(uint64_t index, S3ClientAdaptorImpl* s3ClientAdaptor,
                    std::shared_ptr<KVClientManager> kvClientManager)
      : index_(index),
        s3ClientAdaptor_(s3ClientAdaptor),
        flushingDataCache_(nullptr),
        kvClientManager_(std::move(kvClientManager)) {}
  virtual ~ChunkCacheManager() = default;
  void ReadChunk(uint64_t index, uint64_t chunkPos, uint64_t readLen,
                 char* dataBuf, uint64_t dataBufOffset,
                 std::vector<ReadRequest>* requests);
  virtual void WriteNewDataCache(S3ClientAdaptorImpl* s3ClientAdaptor,
                                 uint32_t chunkPos, uint32_t len,
                                 const char* data);
  virtual void AddReadDataCache(DataCachePtr dataCache);
  virtual DataCachePtr FindWriteableDataCache(
      uint64_t pos, uint64_t len, std::vector<DataCachePtr>* mergeDataCacheVer,
      uint64_t inodeId);
  virtual void ReadByWriteCache(uint64_t chunkPos, uint64_t readLen,
                                char* dataBuf, uint64_t dataBufOffset,
                                std::vector<ReadRequest>* requests);
  virtual void ReadByReadCache(uint64_t chunkPos, uint64_t readLen,
                               char* dataBuf, uint64_t dataBufOffset,
                               std::vector<ReadRequest>* requests);
  virtual void ReadByFlushData(uint64_t chunkPos, uint64_t readLen,
                               char* dataBuf, uint64_t dataBufOffset,
                               std::vector<ReadRequest>* requests);
  virtual DINGOFS_ERROR Flush(uint64_t inodeId, bool force, bool toS3 = false);
  uint64_t GetIndex() { return index_; }
  bool IsEmpty() {
    utils::ReadLockGuard writeCacheLock(rwLockChunk_);
    return (dataWCacheMap_.empty() && dataRCacheMap_.empty());
  }
  virtual void ReleaseReadDataCache(uint64_t key);
  virtual void ReleaseCache();
  void TruncateCache(uint64_t chunkPos);
  void UpdateWriteCacheMap(uint64_t oldChunkPos, DataCache* dataCache);
  // for unit test
  void AddWriteDataCacheForTest(DataCachePtr dataCache);
  void ReleaseCacheForTest() {
    {
      utils::WriteLockGuard writeLockGuard(rwLockWrite_);
      dataWCacheMap_.clear();
    }
    utils::WriteLockGuard writeLockGuard(rwLockRead_);
    dataRCacheMap_.clear();
  }

  utils::RWLock rwLockChunk_;  //  for read write chunk
  utils::RWLock rwLockWrite_;  //  for dataWCacheMap_

 private:
  void ReleaseWriteDataCache(const DataCachePtr& dataCache);
  void TruncateWriteCache(uint64_t chunkPos);
  void TruncateReadCache(uint64_t chunkPos);
  bool IsFlushDataEmpty() { return flushingDataCache_ == nullptr; }

  uint64_t index_;
  std::map<uint64_t, DataCachePtr> dataWCacheMap_;  // first is pos in chunk
  std::map<uint64_t, std::list<DataCachePtr>::iterator>
      dataRCacheMap_;  // first is pos in chunk

  utils::RWLock rwLockRead_;  //  for read cache
  S3ClientAdaptorImpl* s3ClientAdaptor_;
  dingofs::utils::Mutex flushMtx_;
  DataCachePtr flushingDataCache_;
  dingofs::utils::Mutex flushingDataCacheMtx_;

  std::shared_ptr<KVClientManager> kvClientManager_;
};

class FileCacheManager {
 public:
  FileCacheManager(uint32_t fsid, uint64_t inode,
                   S3ClientAdaptorImpl* s3ClientAdaptor,
                   std::shared_ptr<KVClientManager> kvClientManager,
                   std::shared_ptr<utils::TaskThreadPool<>> threadPool)
      : fsId_(fsid),
        inode_(inode),
        s3ClientAdaptor_(s3ClientAdaptor),
        kvClientManager_(std::move(kvClientManager)),
        readTaskPool_(threadPool) {}
  FileCacheManager() = default;
  ~FileCacheManager() = default;

  ChunkCacheManagerPtr FindOrCreateChunkCacheManager(uint64_t index);

  void ReleaseCache();

  virtual void TruncateCache(uint64_t offset, uint64_t fileSize);

  virtual DINGOFS_ERROR Flush(bool force, bool toS3 = false);

  virtual int Write(uint64_t offset, uint64_t length, const char* dataBuf);

  virtual int Read(uint64_t inode_id, uint64_t offset, uint64_t length,
                   char* data_buf);

  bool IsEmpty() { return chunkCacheMap_.empty(); }

  uint64_t GetInodeId() const { return inode_; }

  void SetChunkCacheManagerForTest(uint64_t index,
                                   ChunkCacheManagerPtr chunk_cache_manager) {
    utils::WriteLockGuard lg(rwLock_);

    auto ret = chunkCacheMap_.emplace(index, chunk_cache_manager);
    assert(ret.second);
    (void)ret;
  }

 private:
  void WriteChunk(uint64_t index, uint64_t chunkPos, uint64_t writeLen,
                  const char* dataBuf);
  void GenerateS3Request(ReadRequest request,
                         const pb::metaserver::S3ChunkInfoList& s3ChunkInfoList,
                         char* dataBuf, std::vector<S3ReadRequest>* requests,
                         uint64_t fsId, uint64_t inodeId);

  void PrefetchS3Objs(
      const std::vector<std::pair<blockcache::BlockKey, uint64_t>>&
          prefetchObjs);

  void HandleReadRequest(const ReadRequest& request,
                         const pb::metaserver::S3ChunkInfo& s3ChunkInfo,
                         std::vector<ReadRequest>* addReadRequests,
                         std::vector<uint64_t>* deletingReq,
                         std::vector<S3ReadRequest>* requests, char* dataBuf,
                         uint64_t fsId, uint64_t inodeId);

  int HandleReadRequest(const std::vector<S3ReadRequest>& requests,
                        std::vector<S3ReadResponse>* responses,
                        uint64_t fileLen);

  // GetChunkLoc: get chunk info according to offset
  void GetChunkLoc(uint64_t offset, uint64_t* index, uint64_t* chunkPos,
                   uint64_t* chunkSize);

  // GetBlockLoc: get block info according to offset
  void GetBlockLoc(uint64_t offset, uint64_t* chunkIndex, uint64_t* chunkPos,
                   uint64_t* blockIndex, uint64_t* blockPos);

  // read data from memory read/write cache
  void ReadFromMemCache(uint64_t offset, uint64_t length, char* dataBuf,
                        uint64_t* actualReadLen,
                        std::vector<ReadRequest>* memCacheMissRequest);

  // miss read from memory read/write cache, need read from
  // kv(localdisk/remote cache/s3)
  int GenerateKVRequest(const std::shared_ptr<InodeWrapper>& inode_wrapper,
                        const std::vector<ReadRequest>& read_request,
                        char* data_buf, std::vector<S3ReadRequest>* kv_request);

  enum class ReadStatus {
    OK = 0,
    S3_READ_FAIL = -1,
    S3_NOT_EXIST = -2,
  };

  ReadStatus toReadStatus(blockcache::BCACHE_ERROR rc) {
    ReadStatus st = ReadStatus::OK;
    if (rc != blockcache::BCACHE_ERROR::OK) {
      st = (rc == blockcache::BCACHE_ERROR::NOT_FOUND)
               ? ReadStatus::S3_NOT_EXIST
               : ReadStatus::S3_READ_FAIL;
    }
    return st;
  }

  // read kv request, need
  ReadStatus ReadKVRequest(const std::vector<S3ReadRequest>& kv_requests,
                           char* data_buf, uint64_t file_len);

  // thread function for ReadKVRequest
  void ProcessKVRequest(const S3ReadRequest& req, char* data_buf,
                        uint64_t file_len, std::once_flag& cancel_flag,
                        std::atomic<bool>& is_canceled,
                        std::atomic<blockcache::BCACHE_ERROR>& ret_code);

  // read kv request from local disk cache
  bool ReadKVRequestFromLocalCache(const blockcache::BlockKey& key,
                                   char* buffer, uint64_t offset,
                                   uint64_t length);

  // read kv request from remote cache like memcached
  bool ReadKVRequestFromRemoteCache(const std::string& name, char* databuf,
                                    uint64_t offset, uint64_t length);

  // read kv request from s3
  bool ReadKVRequestFromS3(const std::string& name, char* databuf,
                           uint64_t offset, uint64_t length,
                           blockcache::BCACHE_ERROR* rc);

  // read retry policy when read from s3 occur not exist error
  int HandleReadS3NotExist(uint32_t retry,
                           const std::shared_ptr<InodeWrapper>& inode_wrapper);

  // prefetch for block
  void PrefetchForBlock(const S3ReadRequest& req, uint64_t fileLen,
                        uint64_t blockSize, uint64_t chunkSize,
                        uint64_t startBlockIndex);

  friend class AsyncPrefetchCallback;

  uint64_t fsId_;
  uint64_t inode_;
  std::map<uint64_t, ChunkCacheManagerPtr> chunkCacheMap_;  // first is index
  utils::RWLock rwLock_;
  dingofs::utils::Mutex mtx_;
  S3ClientAdaptorImpl* s3ClientAdaptor_;
  dingofs::utils::Mutex downloadMtx_;
  std::set<std::string> downloadingObj_;

  std::shared_ptr<KVClientManager> kvClientManager_;
  std::shared_ptr<utils::TaskThreadPool<>> readTaskPool_;
};

class FsCacheManager {
 public:
  FsCacheManager(S3ClientAdaptorImpl* s3ClientAdaptor,
                 uint64_t readCacheMaxByte, uint64_t writeCacheMaxByte,
                 uint32_t readCacheThreads,
                 std::shared_ptr<KVClientManager> kvClientManager)
      : lruByte_(0),
        wDataCacheNum_(0),
        wDataCacheByte_(0),
        readCacheMaxByte_(readCacheMaxByte),
        writeCacheMaxByte_(writeCacheMaxByte),
        s3ClientAdaptor_(s3ClientAdaptor),
        isWaiting_(false),
        kvClientManager_(std::move(kvClientManager)) {
    readTaskPool_->Start(readCacheThreads);
  }
  FsCacheManager() = default;
  virtual ~FsCacheManager() { readTaskPool_->Stop(); }
  virtual FileCacheManagerPtr FindFileCacheManager(uint64_t inodeId);
  virtual FileCacheManagerPtr FindOrCreateFileCacheManager(uint64_t fsId,
                                                           uint64_t inodeId);
  void ReleaseFileCacheManager(uint64_t inodeId);

  bool Set(DataCachePtr dataCache, std::list<DataCachePtr>::iterator* outIter);
  bool Delete(std::list<DataCachePtr>::iterator iter);
  void Get(std::list<DataCachePtr>::iterator iter);

  DINGOFS_ERROR FsSync(bool force);
  uint64_t GetDataCacheNum() {
    return wDataCacheNum_.load(std::memory_order_relaxed);
  }

  virtual uint64_t GetDataCacheSize() {
    return wDataCacheByte_.load(std::memory_order_relaxed);
  }

  virtual uint64_t GetDataCacheMaxSize() { return writeCacheMaxByte_; }

  uint64_t GetLruByte() {
    std::lock_guard<std::mutex> lk(lruMtx_);
    return lruByte_;
  }

  void SetFileCacheManagerForTest(uint64_t inodeId,
                                  FileCacheManagerPtr fileCacheManager) {
    utils::WriteLockGuard writeLockGuard(rwLock_);

    auto ret = fileCacheManagerMap_.emplace(inodeId, fileCacheManager);
    assert(ret.second);
    (void)ret;
  }
  void DataCacheNumInc();
  void DataCacheNumFetchSub(uint64_t v);
  void DataCacheByteInc(uint64_t v);
  void DataCacheByteDec(uint64_t v);

 private:
  class ReadCacheReleaseExecutor {
   public:
    ReadCacheReleaseExecutor();
    ~ReadCacheReleaseExecutor();

    void Stop();

    void Release(std::list<DataCachePtr>* caches);

   private:
    void ReleaseCache();

    std::mutex mtx_;
    std::condition_variable cond_;
    std::list<DataCachePtr> retired_;
    std::atomic<bool> running_;
    std::thread t_;
  };

  std::unordered_map<uint64_t, FileCacheManagerPtr>
      fileCacheManagerMap_;  // first is inodeid
  utils::RWLock rwLock_;
  std::mutex lruMtx_;

  std::list<DataCachePtr> lruReadDataCacheList_;
  uint64_t lruByte_;
  std::atomic<uint64_t> wDataCacheNum_;
  std::atomic<uint64_t> wDataCacheByte_;
  uint64_t readCacheMaxByte_;
  uint64_t writeCacheMaxByte_;
  S3ClientAdaptorImpl* s3ClientAdaptor_;
  bool isWaiting_;
  std::mutex mutex_;
  std::condition_variable cond_;

  ReadCacheReleaseExecutor releaseReadCache_;

  std::shared_ptr<KVClientManager> kvClientManager_;

  std::shared_ptr<utils::TaskThreadPool<>> readTaskPool_ =
      std::make_shared<utils::TaskThreadPool<>>();
};

}  // namespace client
}  // namespace dingofs

#endif  // DINGOFS_SRC_CLIENT_S3_CLIENT_S3_CACHE_MANAGER_H_
