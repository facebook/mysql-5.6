/* Copyright (c) 2000, 2016, Oracle and/or its affiliates. All rights
   reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#ifndef SQL_STATS_INCLUDED
#define SQL_STATS_INCLUDED

/**
  Maps used for global and snapshot SQL stats.
*/
template<typename T>
using Stats_map = std::unordered_map<md5_key, T>;
using Sql_stats_map = Stats_map<SQL_STATS *>;
using Sql_text_map = Stats_map<SQL_TEXT *>;
using Client_attrs_map = Stats_map<std::string>;

/**
  @struct Sql_stats_maps
  Collection of maps for SQL stats.
*/
struct Sql_stats_maps
{
  Sql_stats_map *stats;
  Sql_text_map *text;
  Client_attrs_map *client_attrs;
  int ref_count;
  bool drop_maps;
  ulonglong size;
  ulonglong count;

  Sql_stats_maps();
  ~Sql_stats_maps();

  bool init();
  void cleanup();
  bool is_set();
  void move_maps(Sql_stats_maps &other);

private:
  void reset_maps();

  /* Copy of maps is disallowed, use move instead. */
  Sql_stats_maps(const Sql_stats_maps &) = delete;
  Sql_stats_maps &operator=(const Sql_stats_maps &) = delete;
};

#endif /* SQL_STATS_INCLUDED */
