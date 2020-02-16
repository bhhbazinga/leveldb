// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// The representation of a DBImpl consists of a set of Versions.  The
// newest version is called "current".  Older versions may be kept
// around to provide a consistent view to live iterators.
//
// DBImpl的实现包含一系列的Versions，最新的version被称为"current"。
// 老的versions可以保留，用于提供存活的迭代器一个一致性视图。
// Each Version keeps track of a set of Table files per level.  The
// entire set of versions is maintained in a VersionSet.
// 每个version管理每一层中的一系列sstable，所有的versions在VersionSet中
// 维护。
// Version,VersionSet are thread-compatible, but require external
// synchronization on all accesses.
// Version，VersionSet是线程兼容的，但所有的访问需要外部同步机制来作保证。

#ifndef STORAGE_LEVELDB_DB_VERSION_SET_H_
#define STORAGE_LEVELDB_DB_VERSION_SET_H_

#include <map>
#include <set>
#include <vector>

#include "db/dbformat.h"
#include "db/version_edit.h"
#include "port/port.h"
#include "port/thread_annotations.h"

namespace leveldb {

namespace log {
class Writer;
}

class Compaction;
class Iterator;
class MemTable;
class TableBuilder;
class TableCache;
class Version;
class VersionSet;
class WritableFile;

// Return the smallest index i such that files[i]->largest >= key.
// Return files.size() if there is no such file.
// REQUIRES: "files" contains a sorted list of non-overlapping files.
// 找到第一个最大的key>=key的sstable(相当于二分lower_bound)，返回他的下标i，
// 如果不存在这样的文件，就返回files的大小，
// 传入的files必须满足互相之间没有重合。
int FindFile(const InternalKeyComparator& icmp,
             const std::vector<FileMetaData*>& files, const Slice& key);

// Returns true iff some file in "files" overlaps the user key range
// [*smallest,*largest].
// smallest==nullptr represents a key smaller than all keys in the DB.
// largest==nullptr represents a key largest than all keys in the DB.
// REQUIRES: If disjoint_sorted_files, files[] contains disjoint ranges
//           in sorted order.
// 返回files中的key是否与[smallest_user_key, largest_user_key]这个区间重合，
// smallest_user_key == nullptr或largest_user_key==nullptr表示不设上下限。
// REQUIRES:如果disjoint_sorted_files是true， files包含不相交范围??
bool SomeFileOverlapsRange(const InternalKeyComparator& icmp,
                           bool disjoint_sorted_files,
                           const std::vector<FileMetaData*>& files,
                           const Slice* smallest_user_key,
                           const Slice* largest_user_key);

class Version {
 public:
  // Lookup the value for key.  If found, store it in *val and
  // return OK.  Else return a non-OK status.  Fills *stats.
  // REQUIRES: lock is not held
  // 查找key对应的value，如果找到就将它存到*val中，并返回OK状态，
  // 否则返回失败状态。同时，填充*stat中的数据。
  struct GetStats {
    FileMetaData* seek_file;
    int seek_file_level;
  };

  // Append to *iters a sequence of iterators that will
  // yield the contents of this Version when merged together.
  // REQUIRES: This version has been saved (see VersionSet::SaveTo)
  // 在*iters后追加迭代器序列，之后将会在合并时生成这个版本的内容。
  // 当前版本必须已经保存。（参考VersionSet::SaveTo）
  void AddIterators(const ReadOptions&, std::vector<Iterator*>* iters);

  // 查找key对应的value，如果找到就将它存到*val中，并返回OK状态，
  // 否则返回失败状态。同时，填充*stat中的数据。
  Status Get(const ReadOptions&, const LookupKey& key, std::string* val,
             GetStats* stats);

  // Adds "stats" into the current state.  Returns true if a new
  // compaction may need to be triggered, false otherwise.
  // REQUIRES: lock is held
  // 添加一个"stats"到当前state，如果一个新的compaction需要触发就返回
  // true，否则返回false。
  // 需要持有锁
  bool UpdateStats(const GetStats& stats);

  // Record a sample of bytes read at the specified internal key.
  // Samples are taken approximately once every config::kReadBytesPeriod
  // bytes.  Returns true if a new compaction may need to be triggered.
  // REQUIRES: lock is held
  // 记录按特定internal key读取的字节样本??
  // 大约每config::kReadBytesPeriod字节需要进行一次采样??
  // 如果一个新的compaction需要触发就返回
  // true，否则返回false。
  // 需要持有锁
  bool RecordReadSample(Slice key);

  // Reference count management (so Versions do not disappear out from
  // under live iterators)
  void Ref();
  void Unref();

  void GetOverlappingInputs(
      int level,
      const InternalKey* begin,  // nullptr means before all keys
      const InternalKey* end,    // nullptr means after all keys
      std::vector<FileMetaData*>* inputs);

  // Returns true iff some file in the specified level overlaps
  // some part of [*smallest_user_key,*largest_user_key].
  // smallest_user_key==nullptr represents a key smaller than all the DB's keys.
  // largest_user_key==nullptr represents a key largest than all the DB's keys.
  // 返回level层中是否有sstable的key与[smallest_user_key, largest_user_key]这个区间重合，
  // smallest_user_key == nullptr或largest_user_key==nullptr表示不设上下限。
  bool OverlapInLevel(int level, const Slice* smallest_user_key,
                      const Slice* largest_user_key);

  // Return the level at which we should place a new memtable compaction
  // result that covers the range [smallest_user_key,largest_user_key].
  // 返回一个需要存储memtable compaction结果的层，
  // compaction结果覆盖[smallest_user_key,largest_user_key]这个区间
  int PickLevelForMemTableOutput(const Slice& smallest_user_key,
                                 const Slice& largest_user_key);

  // 返回level层的文件数量
  int NumFiles(int level) const { return files_[level].size(); }

  // Return a human readable string that describes this version's contents.
  // 返回可读的version内容的描述
  std::string DebugString() const;

 private:
  friend class Compaction;
  friend class VersionSet;

  class LevelFileNumIterator;

  explicit Version(VersionSet* vset)
      : vset_(vset),
        next_(this),
        prev_(this),
        refs_(0),
        file_to_compact_(nullptr),
        file_to_compact_level_(-1),
        compaction_score_(-1),
        compaction_level_(-1) {}

  Version(const Version&) = delete;
  Version& operator=(const Version&) = delete;

  ~Version();

  Iterator* NewConcatenatingIterator(const ReadOptions&, int level) const;

  // Call func(arg, level, f) for every file that overlaps user_key in
  // order from newest to oldest.  If an invocation of func returns
  // false, makes no more calls.
  //
  // REQUIRES: user portion of internal_key == user_key.
  // 由新到旧的顺序依次遍历每个包含user_key的sstable文件的FileMetaData，
  // 并回调func,如果func返回false则停止遍历。
  // REQUIRES: internal_key的用户部分==user_key
  void ForEachOverlapping(Slice user_key, Slice internal_key, void* arg,
                          bool (*func)(void*, int, FileMetaData*));

  VersionSet* vset_;  // VersionSet to which this Version belongs 当前Version所属的VersionSet
  Version* next_;     // Next version in linked list 链表中的下一个Version
  Version* prev_;     // Previous version in linked list 链表中的上一个Version
  int refs_;          // Number of live refs to this version 这个Version的有效引用计数

  // List of files per level
  // 每层的文件的FileMetaData的列表
  std::vector<FileMetaData*> files_[config::kNumLevels];

  // Next file to compact based on seek stats.
  // 依据allowed_seeks判断的下一个需要合并的sstable的文件的metadata。
  FileMetaData* file_to_compact_;
  int file_to_compact_level_;

  // Level that should be compacted next and its compaction score.
  // Score < 1 means compaction is not strictly needed.  These fields
  // are initialized by Finalize().
  // 下一个需要被合并的level，以及它的分数，分数小于1表示不需要合并。
  // Finalize()执行时，会改变下以数值。
  // compaction分数
  double compaction_score_;
  // 下一个需要合并的层
  int compaction_level_;
};

class VersionSet {
 public:
  VersionSet(const std::string& dbname, const Options* options,
             TableCache* table_cache, const InternalKeyComparator*);
  VersionSet(const VersionSet&) = delete;
  VersionSet& operator=(const VersionSet&) = delete;

  ~VersionSet();

  // Apply *edit to the current version to form a new descriptor that
  // is both saved to persistent state and installed as the new
  // current version.  Will release *mu while actually writing to the file.
  // REQUIRES: *mu is held on entry.
  // REQUIRES: no other thread concurrently calls LogAndApply()
  // 将versionEdit应用到当前的version，同时创建一个新的fd，
  // fd指向的文件被持久化，并且文件内容指向当前version??
  // 当文件成功写入后释放锁。
  // REQUIRES：
  // 1.调用这个函数时持有锁
  // 2.其他线程不会并发调用LogAndApply
  Status LogAndApply(VersionEdit* edit, port::Mutex* mu)
      EXCLUSIVE_LOCKS_REQUIRED(mu);

  // Recover the last saved descriptor from persistent storage.
  // 从存盘数据中恢复上一个保存的描述符??
  Status Recover(bool* save_manifest);

  // Return the current version.
  // 返回当前Version
  Version* current() const { return current_; }

  // Return the current manifest file number
  // 返回当前manifest文件数量
  uint64_t ManifestFileNumber() const { return manifest_file_number_; }

  // Allocate and return a new file number
  // 分配一个新的文件号
  uint64_t NewFileNumber() { return next_file_number_++; }

  // Arrange to reuse "file_number" unless a newer file number has
  // already been allocated.
  // REQUIRES: "file_number" was returned by a call to NewFileNumber().
  // 除非分配了新的文件号，否则重用这个文件号。
  // REQUIRES：file_number是通过NewFileNumber()返回的
  void ReuseFileNumber(uint64_t file_number) {
    if (next_file_number_ == file_number + 1) {
      next_file_number_ = file_number;
    }
  }

  // Return the number of Table files at the specified level.
  // 返回level层的sstable文件数量
  int NumLevelFiles(int level) const;

  // Return the combined file size of all files at the specified level.
  // 返回指定level的文件总大小
  int64_t NumLevelBytes(int level) const;

  // Return the last sequence number.
  // 返回最近写入操作的序列号
  uint64_t LastSequence() const { return last_sequence_; }

  // Set the last sequence number to s.
  // 设置最新的序列号
  void SetLastSequence(uint64_t s) {
    assert(s >= last_sequence_);
    last_sequence_ = s;
  }

  // Mark the specified file number as used.
  // 标记指定的文件号被使用
  void MarkFileNumberUsed(uint64_t number);

  // Return the current log file number.
  uint64_t LogNumber() const { return log_number_; }

  // Return the log file number for the log file that is currently
  // being compacted, or zero if there is no such log file.
  //返回正在合并的log文件号，如果没有则返回0
  uint64_t PrevLogNumber() const { return prev_log_number_; }

  // Pick level and inputs for a new compaction.
  // Returns nullptr if there is no compaction to be done.
  // Otherwise returns a pointer to a heap-allocated object that
  // describes the compaction.  Caller should delete the result.
  // 生成一个从堆中分配的Compaction对象，这个对象包含了一个新的
  // compaction操作所需要的信息。调用者需要自行delete这个指针。
  // 如果不需要合并时返回nullptr??
  Compaction* PickCompaction();

  // Return a compaction object for compacting the range [begin,end] in
  // the specified level.  Returns nullptr if there is nothing in that
  // level that overlaps the specified range.  Caller should delete
  // the result.
  // 返回一个用于合并指定level中key的范围在[begin,end]内的compaction对象,
  // 如果指定level中没有key的范围在[begin,end]内则返回nullptr。
  Compaction* CompactRange(int level, const InternalKey* begin,
                           const InternalKey* end);

  // Return the maximum overlapping data (in bytes) at next level for any
  // file at a level >= 1.
  // 返回下一层中最大重叠的数据大小，level>=1。
  int64_t MaxNextLevelOverlappingBytes();         

  // Create an iterator that reads over the compaction inputs for "*c".
  // The caller should delete the iterator when no longer needed.
  // 返回一个用于遍历Compaction的内容的迭代器，调用者需要自行delete迭代器指针。
  Iterator* MakeInputIterator(Compaction* c);

  // Returns true iff some level needs a compaction.
  // 如果某些level需要合并则返回true。
  bool NeedsCompaction() const {
    Version* v = current_;
    return (v->compaction_score_ >= 1) || (v->file_to_compact_ != nullptr);
  }

  // Add all files listed in any live version to *live.
  // May also mutate some internal state.
  // 将所有live Version的文件加入 *live中，这可能会导致某些内部状态的改变。
  void AddLiveFiles(std::set<uint64_t>* live);

  // Return the approximate offset in the database of the data for
  // "key" as of version "v".
  // 返回指定version的db中的key的大概的偏移位置??
  uint64_t ApproximateOffsetOf(Version* v, const InternalKey& key);

  // Return a human-readable short (single-line) summary of the number
  // of files per level.  Uses *scratch as backing store.
  // 返回每层文件号的可读的描述,
  // 使用scratch作为备份存储??
  struct LevelSummaryStorage {
    char buffer[100];
  };
  const char* LevelSummary(LevelSummaryStorage* scratch) const;

 private:
  class Builder;

  friend class Compaction;
  friend class Version;

  // 重用manifest
  bool ReuseManifest(const std::string& dscname, const std::string& dscbase);

  void Finalize(Version* v);

  // 获取inputs对应的key的范围
  void GetRange(const std::vector<FileMetaData*>& inputs, InternalKey* smallest,
                InternalKey* largest);

  // 
  void GetRange2(const std::vector<FileMetaData*>& inputs1,
                 const std::vector<FileMetaData*>& inputs2,
                 InternalKey* smallest, InternalKey* largest);

  void SetupOtherInputs(Compaction* c);

  // Save current contents to *log
  // 将当前内容保存到log
  Status WriteSnapshot(log::Writer* log);

  // 追加一个新Version
  void AppendVersion(Version* v);

  Env* const env_;
  const std::string dbname_;
  const Options* const options_;
  TableCache* const table_cache_;
  const InternalKeyComparator icmp_;
  uint64_t next_file_number_;
  uint64_t manifest_file_number_;
  uint64_t last_sequence_;
  uint64_t log_number_;
  uint64_t prev_log_number_;  // 0 or backing store for memtable being compacted

  // Opened lazily
  WritableFile* descriptor_file_;
  log::Writer* descriptor_log_;
  Version dummy_versions_;  // Head of circular doubly-linked list of versions. // Version的循环双链表头节点
  Version* current_;        // == dummy_versions_.prev_

  // Per-level key at which the next compaction at that level should start.
  // Either an empty string, or a valid InternalKey.
  // 每一层都有的一个key，表示下一次合并时需要从这个key开始??
  // 可能是一个空string或是有效的InternalKey。
  std::string compact_pointer_[config::kNumLevels];
};

// A Compaction encapsulates information about a compaction.
// Compaction类分装了合并所需要的信息
class Compaction {
 public:
  ~Compaction();

  // Return the level that is being compacted.  Inputs from "level"
  // and "level+1" will be merged to produce a set of "level+1" files.
  // 返回正在合并的level，来自level和level+1层的sstable将会合并到level+1层中。
  int level() const { return level_; }

  // Return the object that holds the edits to the descriptor done
  // by this compaction.
  // 返回本次compaction后生成的VersionEdit??
  VersionEdit* edit() { return &edit_; }

  // "which" must be either 0 or 1
  // which为0返回本层的文件数量，1返回下一层文件数量??
  int num_input_files(int which) const { return inputs_[which].size(); }

  // Return the ith input file at "level()+which" ("which" must be 0 or 1).
  // 返回本次或下一次的第i个被合并的sstable的FileMetaData。
  FileMetaData* input(int which, int i) const { return inputs_[which][i]; }

  // Maximum size of files to build during this compaction.
  // 返回本次合并后生成的最大文件数量
  uint64_t MaxOutputFileSize() const { return max_output_file_size_; }

  // Is this a trivial compaction that can be implemented by just
  // moving a single input file to the next level (no merging or splitting)
  // 返回这个合并是否是一个轻量级的合并，即只是移动单个文件到下一层（不合并也不分割）。
  bool IsTrivialMove() const;

  // Add all inputs to this compaction as delete operations to *edit.
  // 将所有的input加入compaction，表示删除操作的edit??
  void AddInputDeletions(VersionEdit* edit);

  // Returns true if the information we have available guarantees that
  // the compaction is producing data in "level+1" for which no data exists
  // in levels greater than "level+1".
  // 合并信息确保产生的数据位于level+1层，没有额外的数据大于level+1层 ??
  bool IsBaseLevelForKey(const Slice& user_key);

  // Returns true iff we should stop building the current output
  // before processing "internal_key".
  // 如果我们需要在internal_key前停止构建当前的output则返回true。
  // (level + 1和 level + 2层中的文件有过多重叠)
  bool ShouldStopBefore(const Slice& internal_key);

  // Release the input version for the compaction, once the compaction
  // is successful.
  // 合并完成时，发布版本??
  void ReleaseInputs();

 private:
  friend class Version;
  friend class VersionSet;

  Compaction(const Options* options, int level);

  int level_; // 当前合并层
  uint64_t max_output_file_size_; // 最大的输出文件大小
  Version* input_version_; // 输入文件的Version
  VersionEdit edit_; // 用于编辑本次合并的Version修改结果

  // Each compaction reads inputs from "level_" and "level_+1"
  // 当前层和下一层的所有文件的metadata
  std::vector<FileMetaData*> inputs_[2];  // The two sets of inputs

  // State used to check for number of overlapping grandparent files
  // (parent == level_ + 1, grandparent == level_ + 2)
  // 用于检查重叠的grandparent文件状态
  std::vector<FileMetaData*> grandparents_;
  size_t grandparent_index_;  // Index in grandparent_starts_ grandparent的开始的index
  bool seen_key_;             // Some output key has been seen 某些output key被发现??
  int64_t overlapped_bytes_;  // Bytes of overlap between current output
                              // and grandparent files
                              // 当前output与grandparent文件的重叠大小

  // State for implementing IsBaseLevelForKey

  // level_ptrs_ holds indices into input_version_->levels_: our state
  // is that we are positioned at one of the file ranges for each
  // higher level than the ones involved in this compaction (i.e. for
  // all L >= level_ + 2).
  //
  // ??
  size_t level_ptrs_[config::kNumLevels];
};

}  // namespace leveldb

#endif  // STORAGE_LEVELDB_DB_VERSION_SET_H_
