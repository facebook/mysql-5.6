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
