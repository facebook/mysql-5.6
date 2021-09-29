/* Copyright (c) 2016, 2020, Oracle and/or its affiliates. All rights reserved.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2.0,
  as published by the Free Software Foundation.

  This program is also distributed with certain software (including
  but not limited to OpenSSL) that is licensed under separate terms,
  as designated in a particular file or component or in included license
  documentation.  The authors of MySQL hereby grant you an additional
  permission to link the program and your derivative works with the
  separately licensed software that they have included with MySQL.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License, version 2.0, for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

/**
  @file storage/perfschema/pfs_struct.cc
  pfs structs for optimizing memory consumption (implementation).
*/

#include "storage/perfschema/pfs_struct.h"
#include "my_sys.h"
#include "mysql_com.h"
#include "storage/perfschema/pfs_builtin_memory.h"
#include "storage/perfschema/pfs_instr.h"

static PSI_rwlock_key key_rwlock_NAME_ID_MAP_LOCK_name_id_map;
static PSI_rwlock_info all_rwlocks[] = {
    {&key_rwlock_NAME_ID_MAP_LOCK_name_id_map, "NAME_ID_MAP::LOCK_name_id_map",
     0, 0, PSI_DOCUMENT_ME}};

void PFS_name_id_map::init_names() {
  if (names_map_initialized) return;
  int count = array_elements(all_rwlocks);
  const char *category = "pfs";
  mysql_rwlock_register(category, all_rwlocks, count);
  for (auto &map : my_names) {
    map.current_id = 1;
    mysql_rwlock_init(key_rwlock_NAME_ID_MAP_LOCK_name_id_map,
                      &(map.LOCK_name_id_map));
  }
  names_map_initialized = true;
}

void PFS_name_id_map::destroy_names() {
  if (!names_map_initialized) return;

  names_map_initialized = false;
  for (auto &map : my_names) {
    /* free the names in the array then clear it */
    for (char *elt : map.names) pfs_free(&builtin_memory_name_id_map, 1, elt);
    map.names.clear();

    map.map.clear();
    map.current_id = 1;

    mysql_rwlock_destroy(&(map.LOCK_name_id_map));
  }
}

void PFS_name_id_map::cleanup_names() {
  for (auto &map : my_names) {
    mysql_rwlock_wrlock(&(map.LOCK_name_id_map));
    for (char *elt : map.names) pfs_free(&builtin_memory_name_id_map, 1, elt);
    map.names.clear();

    map.map.clear();
    map.current_id = 1;
    mysql_rwlock_unlock(&(map.LOCK_name_id_map));
  }
}

bool PFS_name_id_map::check_name_maps_valid() {
  if (!names_map_initialized) return false;

  int map_name = DB_MAP_NAME;
  for (const auto &map : my_names) {
    assert(map_name < MAX_MAP_NAME);
    if (map.current_id >= my_names_max_size[map_name]) return false;
    map_name++;
  }
  return true;
}

bool PFS_name_id_map::fill_invert_map(enum_name_id_map_name map_name,
                                      ID_NAME_WITHOUT_LOCK_MAP *id_map) {
  assert(map_name < MAX_MAP_NAME);
  NAME_ID_MAP *name_map = &(my_names[map_name]);

  mysql_rwlock_rdlock(&(name_map->LOCK_name_id_map)); /* read */

  for (auto it = name_map->map.begin(); it != name_map->map.end(); it++)
    id_map->insert(std::make_pair(it->second, it->first.c_str()));

  mysql_rwlock_unlock(&(name_map->LOCK_name_id_map));

  return true;
}

uint PFS_name_id_map::get_id(enum_name_id_map_name map_name, const char *name,
                             uint length) {
  assert(map_name < MAX_MAP_NAME);
  NAME_ID_MAP *map = &(my_names[map_name]);
  uint table_id = INVALID_NAME_ID;
  bool write_lock = false;
  bool retry = true;

  while (retry) {
    /* we use a read lock first hoping to find a match for the
    ** provided name and retry with write lock in case a match
    ** is not found (write lock is required to update the map)
    */
    if (write_lock)
      mysql_rwlock_wrlock(&(map->LOCK_name_id_map)); /* write */
    else
      mysql_rwlock_rdlock(&(map->LOCK_name_id_map)); /* read */

    retry = false;
    auto iter = map->map.find(std::string(name, length));

    if (iter != map->map.end()) /* found a match: return the ID */
      table_id = iter->second;
    else if (!check_name_maps_valid()) {
      /* one of the name maps is invalid */
      table_id = INVALID_NAME_ID;
    } else if (write_lock) {
      /* enter a new entry of we have a write lock */
      assert(length <= 256);
      char *str = (char *)pfs_malloc(&builtin_memory_name_id_map, length + 1,
                                     MYF(MY_WME | MY_ZEROFILL));
      memcpy(str, name, length);
      str[length] = '\0';

      map->names.emplace_back(str);
      map->map.emplace(str, map->current_id);
      table_id = map->current_id++;
    } else {
      /* release the read lock and get a write lock */
      retry = true; /* we only want to retry with a write lock */
      write_lock = true;
      mysql_rwlock_unlock(&(map->LOCK_name_id_map));
    }
  }

  mysql_rwlock_unlock(&(map->LOCK_name_id_map));

  return table_id;
}

const char *PFS_name_id_map::get_name(ID_NAME_WITHOUT_LOCK_MAP *id_map,
                                      uint id) {
  auto elt = id_map->find(id);
  if (elt == id_map->end())
    return nullptr;
  else
    return elt->second.c_str();
}
