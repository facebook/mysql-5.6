#ifndef LOGGER_H
#define LOGGER_H

#include <log.h>
#include "rocksdb/env.h"

class Logger : public rocksdb::Logger {
 public:
  using rocksdb::Logger::Logv;

   // Write an entry to the log file with the specified format.
  void Logv(const char* format, va_list ap) {
    std::string f("LibRocksDB:");
    f.append(format);
    error_log_print(INFORMATION_LEVEL, f.c_str(), ap);
  }
};

#endif
