/*
   Copyright (c) 2015, Facebook, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#ifndef LOGGER_H
#define LOGGER_H

#include <log.h>
#include <sstream>
#include <string>

class RdbLogger : public rocksdb::Logger {
 public:
  virtual void Logv(const rocksdb::InfoLogLevel log_level,
            const char* format,
            va_list ap) {
    enum loglevel mysql_log_level;

    if (rocksdb_logger_) {
      rocksdb_logger_->Logv(log_level, format, ap);
    }

    if (log_level < rdb_log_level_) {
      return;
    }

    if (log_level >= rocksdb::InfoLogLevel::ERROR_LEVEL) {
      mysql_log_level= ERROR_LEVEL;
    } else if (log_level >= rocksdb::InfoLogLevel::WARN_LEVEL) {
      mysql_log_level= WARNING_LEVEL;
    } else {
      mysql_log_level= INFORMATION_LEVEL;
    }

    // log to MySQL
    std::string f("LibRocksDB:");
    f.append(format);
    error_log_print(mysql_log_level, f.c_str(), ap);
  }

  virtual void Logv(const char* format, va_list ap) {
    // If no level is specified, it is by default at information level
    Logv(rocksdb::InfoLogLevel::INFO_LEVEL, format, ap);
  }

  void SetRocksDBLogger(std::shared_ptr<rocksdb::Logger> logger) {
    rocksdb_logger_ = logger;
  }

  void SetRdbLogLevel(const rocksdb::InfoLogLevel log_level) {
    /*
      The log_level parameter is stored in this class instead of using the
      one in the base rocksdb::Logger class.

      There is some obscure problem that occassionally triggers the following
      ASan failure during system shutdown:

      AddressSanitizer CHECK failed:
      ../../.././libsanitizer/asan/asan_rtl.cc:397 "((curr_thread)) != (0)"
      (0x0, 0x0)

      along with the following two functions in the stack trace:

      rocksdb::port::Mutex::Lock()
      rocksdb::ThreadLocalPtr::StaticMeta::OnThreadExit(void*)

      in the rocksdb.optimize_table test when the myrocks plugin attempts to
      set the log_level_ private member of the rocksdb::Logger class, whether
      through the rocksdb::Logger constructor, or the SetInfoLogLevel()
      function. The root cause of the problem is not known, but could be
      related to ordering of deinitializers.

      Storing the level in RdbLogger works around the issue for now.
    */
    rdb_log_level_= log_level;
  }

 private:
  std::shared_ptr<rocksdb::Logger> rocksdb_logger_;
  rocksdb::InfoLogLevel rdb_log_level_;
};

#endif
