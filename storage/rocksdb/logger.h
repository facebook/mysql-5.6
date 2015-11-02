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
#include <string>

class Logger : public rocksdb::Logger {
 public:
  using rocksdb::Logger::Logv;

  void Logv(const rocksdb::InfoLogLevel log_level,
            const char* format,
            va_list ap) {
    static const char* kInfoLogLevelNames[5] = { "DEBUG", "INFO", "WARN",
      "ERROR", "FATAL" };
    if (log_level < GetInfoLogLevel()) {
      return;
    }

    rocksdb::Logger::Logv(log_level, format, ap);

    // log to MySQL if not 'info' level
    if (log_level != rocksdb::InfoLogLevel::INFO_LEVEL) {
      char new_format[500];
      snprintf(new_format, sizeof(new_format) - 1, "[%s] %s",
        kInfoLogLevelNames[log_level], format);

      std::string f("LibRocksDB:");
      f.append(new_format);
      error_log_print(INFORMATION_LEVEL, f.c_str(), ap);
    }
  }


  // Write an entry to the log file using the proper logger.
  void Logv(const char* format, va_list ap) {
    rocksdb_logger->Logv(format, ap);
  }

  std::shared_ptr<rocksdb::Logger> rocksdb_logger;
};

#endif
