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

namespace myrocks {

class Logger : public rocksdb::Logger {
 public:
  using rocksdb::Logger::Logv;

  void Logv(const rocksdb::InfoLogLevel log_level,
            const char* format,
            va_list ap) {
    DBUG_ASSERT(format != nullptr);

    enum loglevel mysql_log_level;

    if (rocksdb_logger_) {
      rocksdb_logger_->Logv(log_level, format, ap);
    }

    if (log_level < GetInfoLogLevel()) {
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

  void Logv(const char* format, va_list ap) {
    DBUG_ASSERT(format != nullptr);
    // If no level is specified, it is by default at information level
    Logv(rocksdb::InfoLogLevel::INFO_LEVEL, format, ap);
  }

  void SetRocksDBLogger(std::shared_ptr<rocksdb::Logger> logger) {
    rocksdb_logger_ = logger;
  }

 private:
  std::shared_ptr<rocksdb::Logger> rocksdb_logger_;
};

}  // namespace myrocks

#endif
