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

#ifndef PFS_STRUCT_H
#define PFS_STRUCT_H

/**
  @file storage/perfschema/pfs_struct.h
  pfs structs for optimizing memory consumption
*/

#include <string.h>
#include <sys/types.h>
#include <array>
#include <string>
#include <unordered_map>
#include <vector>
#include "mysql/components/services/bits/mysql_rwlock_bits.h"

/*
** enum_name_id_map_name
**
** Types of names that we encode using an ID in the statistics structures
** The enum is used to index into array of maps
*/
enum enum_name_id_map_name { DB_MAP_NAME = 0, USER_MAP_NAME = 1, MAX_MAP_NAME };

/*
** NAME_ID_MAP
**
** Used to store mapping of various object names used in the statistics
** structures (DB, user, schema, table, index) to an internal ID. An ID
** is used instead of the object name. Names used to be stored on fixed
** size string fields (size NAME_LEN+1=64*3+1=193).
**
*/
typedef struct st_name_id_map {
  std::vector<char *> names;
  std::unordered_map<std::string, uint> map;

  uint current_id; /* ID sequence, starting from 1 */

  /* Mutex to control access or modification to the array and map */
  mysql_rwlock_t LOCK_name_id_map;
} NAME_ID_MAP;

/*
** ID_NAME_WITHOUT_LOCK_MAP
**
** Stores the inverse mapping of the data stored in NAME_ID_MAP. Used
** only when returning rows for *_pfs tables where we fetch a
** name for an ID and this makes the lookup O(1) instead of O(n).
** It is built in the constructor of pfs tables then freed
** when pfs tables are destructed.
*/
typedef std::unordered_map<uint, std::string> ID_NAME_WITHOUT_LOCK_MAP;

#define INVALID_NAME_ID UINT_MAX

class PFS_name_id_map {
 public:
  /*
  ** init_names
  **
  ** Initialize the name maps for all object names we care about.
  ** Called when we start tracking stats for pfs tables.
  */
  void init_names();

  /*
  ** destroy_names
  **
  ** Destroy the name maps. Called when we stop tracking stats for pfs tables.
  */
  void destroy_names();

  /*
  ** cleanup_names
  **
  ** Cleanup the name maps.
  */
  void cleanup_names();

  /*
  ** fill_invert_map
  ** Populate id-to-name map from name-to-id map to allow efficient
  ** lookup of names based on an ID when fetching data for pfs tables
  */
  bool fill_invert_map(enum_name_id_map_name map_name,
                       ID_NAME_WITHOUT_LOCK_MAP *id_map);

  /*
  ** get_id
  **
  ** Returns the ID for a given name (db, table, etc).
  ** If the name is already in the map then it returns its ID, else
  ** it'll either reuse the ID of a dropped name or create a new ID
  ** It returns INVALID_NAME_ID in case of an exception, e.g, if we
  ** reach the maximum capacity of the map
  */
  uint get_id(enum_name_id_map_name map_name, const char *name, uint length);

  /*
  ** check_name_maps_valid
  **
  ** Check that the name maps are valid, i.e, new names can be added
  ** It is called in get_id() which returns INVALID_NAME_ID if this
  ** function returns false.
  */
  bool check_name_maps_valid();

  /*
  ** get_name - get a name from an ID
  **  Lookup the inverted map for the specified ID
  **  The inverted map is built for every query to return the stats
  **  at the beginning of the associated pfs table.
  */
  const char *get_name(ID_NAME_WITHOUT_LOCK_MAP *id_map, uint id);

 private:
  bool names_map_initialized = false;

  std::array<NAME_ID_MAP, MAX_MAP_NAME> my_names;

  /*
  ** maximum size of each map:
  ** - DB:    1024 * 1024
  ** - User:  1024 * 1024
  ** These are absolute maximum and when reached will lead to
  ** no more objects added to their respective maps. Function
  ** check_name_maps_valid() will return FALSE if any of the
  ** maps is full.
  **
  ** find_or_create_* functions used while ingesting pfs* values
  ** always enforce max size of rows allowed which is enough to
  ** enforce limits.
  */
  const std::array<uint, MAX_MAP_NAME> my_names_max_size = {
      {(1024 * 1024), (1024 * 1024)}};
};
#endif
