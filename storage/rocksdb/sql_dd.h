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

#pragma once
#include "sql/dd/properties.h"
#include "sql/dd/types/column.h"
#include "sql/dd/types/table.h"
#include "sql/table.h"

namespace myrocks {

/** RocksDB private keys for dd::Table */
enum dd_table_keys {
  /** Columns before first instant ADD COLUMN */
  DD_TABLE_INSTANT_COLS,
  /** Sentinel */
  DD_TABLE__LAST
};

/** RocksDB private key strings for dd::Table. @see dd_table_keys */
const char *const dd_table_key_strings[DD_TABLE__LAST] = {"instant_col"};

/** Look up a column in a table using the system_charset_info collation.
@param[in]	dd_table	data dictionary table
@param[in]	name		column name
@return the column
@retval nullptr if not found */
const dd::Column *dd_find_column(const dd::Table *dd_table, const char *name);

/** RocksDB private keys for dd::Column */
/** About the DD_INSTANT_COLUMN_DEFAULT*, please note that if it's a
partitioned table, not every default value is needed for every partition.
For example, after ALTER TABLE ... PARTITION, maybe some partitions only
need part or even none of the default values. Let's say there are two
partitions, p1 and p2. p1 needs 10 default values while p2 needs 2.
If another ALTER ... PARTITION makes p1 a fresh new partition which
doesn't need the default values any more, currently, the extra 8(10 - 2)
default values are not removed form dd::Column::se_private_data. */
enum dd_column_keys {
  /** Default value when it was added instantly */
  DD_INSTANT_COLUMN_DEFAULT,
  /** Default value is null or not */
  DD_INSTANT_COLUMN_DEFAULT_NULL,
  /** Sentinel */
  DD_COLUMN__LAST
};

/** RocksDB private key strings for dd::Column, @see dd_column_keys */
const char *const dd_column_key_strings[DD_COLUMN__LAST] = {"default",
                                                            "default_null"};
#ifdef UNIV_DEBUG
/** Check if the instant columns are consistent with the se_private_data
in dd::Table
@param[in]	dd_table	dd::Table
@return true if consistent, otherwise false */
bool dd_instant_columns_exist(const dd::Table &dd_table);
#endif /* UNIV_DEBUG */

/** Copy the engine-private parts of column definitions of a table.
 @param[in,out]  new_table Copy of old table
 @param[in]  old_table Old table */
void dd_copy_table_columns(dd::Table &new_table, const dd::Table &old_table);

/** Copy the engine-private parts of a table or partition definition
when the change does not affect InnoDB. This mainly copies the common
private data between dd::Table and dd::Partition
@tparam		Table		dd::Table or dd::Partition
@param[in,out]	new_table	Copy of old table or partition definition
@param[in]	old_table	Old table or partition definition
@param[in]	copy_old_options Clear options on new table and copy from old */
void dd_copy_private(dd::Table &new_table, const dd::Table &old_table,
                     bool copy_old_options = true);

/** Determine if a dd::Table has any instant column
@param[in]	table	dd::Table
@return	true	If it's a table with instant columns
@retval	false	Not a table with instant columns */
inline bool dd_table_has_instant_cols(const dd::Table &table) {
  bool instant = table.se_private_data().exists(
      dd_table_key_strings[DD_TABLE_INSTANT_COLS]);

#ifdef UNIV_DEBUG
  assert(!instant || dd_instant_columns_exist(table));
#endif /* UNIV_DEBUG */

  return (instant);
}

void dd_table_get_instant_default(const dd::Column &column,
                                  uchar **default_value, size_t *len);

void dd_add_instant_columns(const TABLE *old_table, const TABLE *altered_table,
                            dd::Table *new_dd_table MY_ATTRIBUTE((unused)));

/** Class to decode or encode a stream of default value for instant table.
The decode/encode are necessary because that the default values would b
kept as RocksDB format stream, which is in fact byte stream. However,
to store them in the DD se_private_data, it requires text(char).
So basically, the encode will change the byte stream into char stream,
by spliting every byte into two chars, for example, 0xFF, would be split
into 0x0F and 0x0F. So the final storage space would be double. For the
decode, it's the converse process, combining two chars into one byte. */
class DD_instant_col_val_coder {
 public:
  /** Encode the specified stream in format of bytes into chars
  @param[in]	stream	stream to encode in bytes
  @param[in]	in_len	length of the stream
  @param[out]	out_len	length of the encoded stream
  @return	the encoded stream, which would be destroyed */
  std::unique_ptr<char[]> encode(const unsigned char *stream, size_t in_len,
                                 size_t *out_len);

  /** Decode the specified stream, which is encoded by encode()
  @param[in]	stream	stream to decode in chars
  @param[in]	in_len	length of the stream
  @param[out]	out_len	length of the decoded stream
  @return	the decoded stream, which would be destroyed by caller */
  uchar *decode(const char *stream, size_t in_len, size_t *out_len);
};

/** Update metadata in commit phase for INSTANT_NO_CHANGE.
Note this function should only update the metadata which would not result
in failure
@param[in]     old_dd_tab      Old dd::Table
@param[in,out] new_dd_tab      New dd::Table */
inline void dd_commit_inplace_no_change(const dd::Table *old_dd_tab,
                                        dd::Table *new_dd_tab) {
  dd_copy_private(*new_dd_tab, *old_dd_tab);
  dd_copy_table_columns(*new_dd_tab, *old_dd_tab);
}

/** Update metadata in commit phase for instant ADD COLUMN. Basically, it
should remember number of instant columns, and the default value of newly
added columns.
Note this function should only update the metadata which would not result
in failure
@param[in]     old_table       MySQL table as it is before the ALTER operation
@param[in]     altered_table   MySQL table that is being altered
@param[in]     old_dd_tab      Old dd::Table
@param[in,out] new_dd_tab      New dd::Table */
inline void dd_commit_instant_table(
    const TABLE *old_table MY_ATTRIBUTE((unused)),
    const TABLE *altered_table MY_ATTRIBUTE((unused)),
    const dd::Table *old_dd_tab MY_ATTRIBUTE((unused)), dd::Table *new_dd_tab) {
  assert(!new_dd_tab->is_temporary());
  assert(old_dd_tab->columns().size() <= new_dd_tab->columns()->size());

  if (!new_dd_tab->se_private_data().exists(
          dd_table_key_strings[DD_TABLE_INSTANT_COLS])) {
    uint32_t instant_cols = old_table->visible_field_count();

    if (old_dd_tab->se_private_data().exists(
            dd_table_key_strings[DD_TABLE_INSTANT_COLS])) {
      old_dd_tab->se_private_data().get(
          dd_table_key_strings[DD_TABLE_INSTANT_COLS], &instant_cols);
    }

    new_dd_tab->se_private_data().set(
        dd_table_key_strings[DD_TABLE_INSTANT_COLS], instant_cols);
  }

  /* Copy all old instant cols default values in old dd tab if exist */
  dd_copy_table_columns(*new_dd_tab, *old_dd_tab);

  /* Then add all new default values */
  dd_add_instant_columns(old_table, altered_table, new_dd_tab);

  assert(dd_table_has_instant_cols(*new_dd_tab));
}
}  // namespace myrocks
