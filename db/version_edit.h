// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef STORAGE_LEVELDB_DB_VERSION_EDIT_H_
#define STORAGE_LEVELDB_DB_VERSION_EDIT_H_

#include <set> 
#include <utility>
#include <vector>

#include "db/dbformat.h"

namespace leveldb {

class VersionSet;

struct FileMetaData {
  FileMetaData() : refs(0), allowed_seeks(1 << 30), file_size(0) {}

  int refs; // 引用计数
  
  int allowed_seeks;  // Seeks allowed until compaction 在合并前允许的最大seek次数
  uint64_t number; // 文件号
  uint64_t file_size;    // File size in bytes  文件大小
  InternalKey smallest;  // Smallest internal key served by table sstable中最小的key
  InternalKey largest;   // Largest internal key served by table sstable中最大的key
};

class VersionEdit {
 public:
  VersionEdit() { Clear(); }
  ~VersionEdit() {}

  void Clear();

  // 设置比较器名字
  void SetComparatorName(const Slice& name) {
    has_comparator_ = true;
    comparator_ = name.ToString();
  }

  // 设置log文件号
  void SetLogNumber(uint64_t num) {
    has_log_number_ = true;
    log_number_ = num;
  }

  // 设置前一个log文件号
  void SetPrevLogNumber(uint64_t num) {
    has_prev_log_number_ = true;
    prev_log_number_ = num;
  }

  // 设置下一个文件号
  void SetNextFile(uint64_t num) {
    has_next_file_number_ = true;
    next_file_number_ = num;
  }

  // 设置最新序列号
  void SetLastSequence(SequenceNumber seq) {
    has_last_sequence_ = true;
    last_sequence_ = seq;
  }

  // 设置合并点??
  void SetCompactPointer(int level, const InternalKey& key) {
    compact_pointers_.push_back(std::make_pair(level, key));
  }

  // Add the specified file at the specified number.
  // REQUIRES: This version has not been saved (see VersionSet::SaveTo)
  // REQUIRES: "smallest" and "largest" are smallest and largest keys in file
  // 在level层中新增文件。
  // REQUIRES: 当前Version没有被保存
  // REQUIRES: "smallest" 和 "largest"是文件中的最小、最大的key
  void AddFile(int level, uint64_t file, uint64_t file_size,
               const InternalKey& smallest, const InternalKey& largest) {
    FileMetaData f;
    f.number = file;
    f.file_size = file_size;
    f.smallest = smallest;
    f.largest = largest;
    new_files_.push_back(std::make_pair(level, f));
  }

  // Delete the specified "file" from the specified "level".
  // 删除level层中的制定文件，file为文件号
  void DeleteFile(int level, uint64_t file) {
    deleted_files_.insert(std::make_pair(level, file));
  }

  // 将VersioinEdit数据序列化到dst中
  void EncodeTo(std::string* dst) const;
  Status DecodeFrom(const Slice& src);

  std::string DebugString() const;

 private:
  friend class VersionSet;

  typedef std::set<std::pair<int, uint64_t> > DeletedFileSet; // 删除文件的集合

  std::string comparator_;
  uint64_t log_number_; // log文件号
  uint64_t prev_log_number_; // 前一个log文件号
  uint64_t next_file_number_; // 下一个文件号
  SequenceNumber last_sequence_; // 最新的序列号
  bool has_comparator_; // 是否有比较器
  bool has_log_number_; // 是否有log文件号
  bool has_prev_log_number_; // 是否有前一个log文件号
  bool has_next_file_number_; // 是否有下一个文件号
  bool has_last_sequence_; // 是否有最新的序列号

  std::vector<std::pair<int, InternalKey> > compact_pointers_; // 合并点集合??
  DeletedFileSet deleted_files_;
  std::vector<std::pair<int, FileMetaData> > new_files_; // 新增的文件
};

}  // namespace leveldb

#endif  // STORAGE_LEVELDB_DB_VERSION_EDIT_H_
