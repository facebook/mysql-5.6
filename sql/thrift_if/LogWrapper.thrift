#!/usr/local/bin/thrift

struct LogMetadata {
  1: optional i32 uncompressed_size;
  2: optional string compression_codec;
  3: optional i64 compression_dict_id;
  4: optional binary compression_dict;
}

struct LogData {
  1: optional binary payload;
}

struct LogEntry {
  1: optional LogData data;
  2: optional LogMetadata metadata;
}
