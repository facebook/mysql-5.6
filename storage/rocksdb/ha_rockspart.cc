/* Copyright (C) 2018 Percona

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
   USA */
#include "ha_rockspart.h"
#include "ha_rocksdb.h"

using myrocks::ha_rocksdb;
namespace myrocks {
extern handlerton *rocksdb_hton;
}

bool ha_rockspart::init_with_fields() {
  /*
    MySql layer calls this before initializing actual partitions and then makes
    decisions basing on the result of updated table_flags.
    As RocksDB enables HA_PRIMARY_KEY_IN_READ_INDEX flag if PK is present and
    disables it otherwise, we can cache this flag earlier, even if partitions
    handlers are not created yet.
  */
  const uint pk = table_share->primary_key;
  if (pk != MAX_KEY) {
    cached_table_flags |= HA_PRIMARY_KEY_IN_READ_INDEX;
  } else {
    cached_table_flags &= ~HA_PRIMARY_KEY_IN_READ_INDEX;
  }

  return Partition_base::init_with_fields();
}

handler *ha_rockspart::get_file_handler(TABLE_SHARE *share,
                                        MEM_ROOT *alloc) const {
  ha_rocksdb *file = new (alloc) ha_rocksdb(myrocks::rocksdb_hton, share);
  file->init();
  return file;
}

void ha_rockspart::set_pk_can_be_decoded_for_each_partition() {
  for (auto file = reinterpret_cast<ha_rocksdb **>(m_file); *file; file++)
    (*file)->set_pk_can_be_decoded(m_pk_can_be_decoded);
}

int ha_rockspart::open(const char *name, int mode, uint test_if_locked,
                       const dd::Table *table_def) {
  int result =
      native_part::Partition_base::open(name, mode, test_if_locked, table_def);
  set_pk_can_be_decoded_for_each_partition();
  return result;
}

int ha_rockspart::create(const char *name, TABLE *form,
                         HA_CREATE_INFO *create_info, dd::Table *table_def) {
  int result =
      native_part::Partition_base::create(name, form, create_info, table_def);
  set_pk_can_be_decoded_for_each_partition();
  return result;
}

/**
  Clone the open and locked partitioning handler.

  @param  mem_root  MEM_ROOT to use.

  @return Pointer to the successfully created clone or nullptr

  @details
  This function creates a new native_part::Partition_base handler as a
  clone/copy. The clone will use the original m_part_info. It also allocates
  memory for ref + ref_dup. In native_part::Partition_base::open()
  a new partition handlers are created (on the passed mem_root)
  for each partition and are also opened.
*/

handler *ha_rockspart::clone(const char *name, MEM_ROOT *mem_root) {
  ha_rockspart *new_handler;

  DBUG_ENTER("ha_rockspart::clone");

  /* If this->table == nullptr, then the current handler has been created but
  not opened. Prohibit cloning such handler. */
  if (!table) DBUG_RETURN(nullptr);

  new_handler =
      new (mem_root) ha_rockspart(ht, table_share, m_part_info, this, mem_root);

  if (!new_handler) DBUG_RETURN(nullptr);

  /*
    Allocate new_handler->ref here because otherwise ha_open will allocate it
    on this->table->mem_root and we will not be able to reclaim that memory
    when the clone handler object is destroyed.
  */
  if (!(new_handler->ref =
            (uchar *)mem_root->Alloc(ALIGN_SIZE(ref_length) * 2)))
    goto err;

  /* We will not use clone() interface to clone individual partition
  handlers. This is because rocksdb_create_handler() gives ha_tokupart handler
  instead of ha_rocksdb handlers. This happens because of presence of parition
  info in TABLE_SHARE. New partition handlers are created for each partiton
  in native_part::Partition_base::open() */
  if (new_handler->ha_open(table, name, table->db_stat,
                           HA_OPEN_IGNORE_IF_LOCKED, nullptr))
    goto err;

  new_handler->m_pk_can_be_decoded = m_pk_can_be_decoded;
  new_handler->set_pk_can_be_decoded_for_each_partition();

  DBUG_RETURN((handler *)new_handler);

err:
  delete new_handler;
  DBUG_RETURN(nullptr);
}

ulong ha_rockspart::index_flags(uint idx, uint part, bool all_parts) const {
  return myrocks::ha_rocksdb::index_flags(m_pk_can_be_decoded, table_share, idx,
                                          part, all_parts);
}
