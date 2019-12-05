// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef STORAGE_LEVELDB_DB_WRITE_BATCH_INTERNAL_H_
#define STORAGE_LEVELDB_DB_WRITE_BATCH_INTERNAL_H_

#include "db/dbformat.h"
#include "leveldb/write_batch.h"

namespace leveldb {

class MemTable;

// WriteBatchInternal provides static methods for manipulating a
// WriteBatch that we don't want in the public WriteBatch interface.
class WriteBatchInternal {
 public:
  // Return the number of entries in the batch.
  // 返回WriteBatch中的记录总数
  static int Count(const WriteBatch* batch);

  // Set the count for the number of entries in the batch.
  // 设置WriteBatch中的记录总数
  static void SetCount(WriteBatch* batch, int n);

  // Return the sequence number for the start of this batch.
  static SequenceNumber Sequence(const WriteBatch* batch);

  // Store the specified number as the sequence number for the start of
  // this batch.
  // 设置WriteBatch的序列号,保证此次批量写入的所有记录的序列号一致
  static void SetSequence(WriteBatch* batch, SequenceNumber seq);

  // 返回的WriteBatch内容
  static Slice Contents(const WriteBatch* batch) {
    return Slice(batch->rep_);
  }

  // 返回WriteBatch内容的字节大小
  static size_t ByteSize(const WriteBatch* batch) {
    return batch->rep_.size();
  }

  // 设置WriteBatch内容
  static void SetContents(WriteBatch* batch, const Slice& contents);

  // 将WriteBatch的内容写入memtable
  static Status InsertInto(const WriteBatch* batch, MemTable* memtable);

  // 合并两个WriteBatch
  static void Append(WriteBatch* dst, const WriteBatch* src);
};

}  // namespace leveldb


#endif  // STORAGE_LEVELDB_DB_WRITE_BATCH_INTERNAL_H_
