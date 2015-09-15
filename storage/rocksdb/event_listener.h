/*
   Copyright (c) 2012, Monty Program Ab

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

#ifndef EVENT_LISTENER_H
#define EVENT_LISTENER_H

struct Table_ddl_manager;

#include "rocksdb/listener.h"

class MyRocksEventListener : public rocksdb::EventListener {
 public:
  explicit MyRocksEventListener(Table_ddl_manager* ddl_manager) :
      ddl_manager_(ddl_manager) {
  }

  void OnCompactionCompleted(
    rocksdb::DB *db, const rocksdb::CompactionJobInfo& ci);
  void OnFlushCompleted(
    rocksdb::DB* db, const rocksdb::FlushJobInfo& flush_job_info);

 private:
  Table_ddl_manager* ddl_manager_;
};

#endif
