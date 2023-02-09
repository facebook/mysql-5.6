/*
  Copyright (C) 2022, Laurynas Biveinis

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

#ifndef CLONE_CLIENT_H_
#define CLONE_CLIENT_H_

#include "my_inttypes.h"
#include "sql/handler.h"

namespace myrocks {

int rocksdb_clone_apply_begin(handlerton *hton, THD *thd, const uchar *&loc,
                              uint &loc_len, uint &task_id, Ha_clone_mode mode,
                              const char *data_dir);

int rocksdb_clone_apply(handlerton *hton, THD *thd, const uchar *loc,
                        uint loc_len, uint task_id, int in_err,
                        Ha_clone_cbk *cbk);

int rocksdb_clone_apply_end(handlerton *hton, THD *thd, const uchar *loc,
                            uint loc_len, uint task_id, int in_err);

namespace clone {

void client_shutdown();

}  // namespace clone

}  // namespace myrocks

#endif
