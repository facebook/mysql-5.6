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

#include "sql_dd.h"
#include <assert.h>
#include <memory>
#include "./rdb_utils.h"
#include "my_sys.h"
#include "mysql/psi/psi_memory.h"
#include "mysql/service_mysql_alloc.h"

namespace myrocks {

#ifdef UNIV_DEBUG
bool dd_instant_columns_exist(const dd::Table &dd_table) {
  uint32_t n_cols = 0;
  uint32_t non_instant_cols = 0;
  bool found = false;

  ut_ad(dd_table.se_private_data().exists(
      dd_table_key_strings[DD_TABLE_INSTANT_COLS]));

  dd_table.se_private_data().get(dd_table_key_strings[DD_TABLE_INSTANT_COLS],
                                 &n_cols);

  for (auto col : dd_table.columns()) {
    // virtual cols aren't stored in records
    if (col->is_virtual() || col->is_se_hidden()) {
      continue;
    }

    const dd::Properties &col_private = col->se_private_data();
    if (col_private.exists(
            dd_column_key_strings[DD_INSTANT_COLUMN_DEFAULT_NULL]) ||
        col_private.exists(dd_column_key_strings[DD_INSTANT_COLUMN_DEFAULT])) {
      found = true;
      continue;
    }

    // Only count non virtual/hidden cols
    ++non_instant_cols;
  }

  ut_ad(found);
  /* Please note that n_cols could be 0 if the table only had some virtual
  columns before instant ADD COLUMN. So below check should be sufficient */
  ut_ad(non_instant_cols == n_cols);
  return (found && non_instant_cols == n_cols);
}
#endif /* UNIV_DEBUG */

/** Add column default values for new instantly appended columns
    Currently This function only support append column
@param[in]	old_table	MySQL table as it is before the ALTER operation
@param[in]	altered_table	MySQL table that is being altered
@param[in,out]	new_dd_table	New dd::Table */
void dd_add_instant_columns(const TABLE *old_table, const TABLE *altered_table,
                            dd::Table *new_dd_table MY_ATTRIBUTE((unused))) {
  assert(altered_table->s->fields > old_table->s->fields);

#ifdef UNIV_DEBUG
  for (uint32_t i = 0; i < old_table->s->fields; ++i) {
    assert(strcmp(old_table->field[i]->field_name,
                  altered_table->field[i]->field_name) == 0);
  }
#endif /* UNIV_DEBUG */

  DD_instant_col_val_coder coder;
#ifdef UNIV_DEBUG
  uint16_t num_instant_cols = 0;
#endif /* UNIV_DEBUG */
  for (uint32_t i = old_table->s->fields; i < altered_table->s->fields; ++i) {
    Field *field = altered_table->field[i];
    dd::Column *column = const_cast<dd::Column *>(
        dd_find_column(new_dd_table, field->field_name));
    assert(column != nullptr);
    // For default null column, add DD_INSTANT_COLUMN_DEFAULT_NULL=1 to
    // se_private field in mysql.columns table
    dd::Properties &se_private = column->se_private_data();
    if (field->is_real_null()) {
      se_private.set(dd_column_key_strings[DD_INSTANT_COLUMN_DEFAULT_NULL],
                     true);
      continue;
    }

#ifdef UNIV_DEBUG
    // ER_BLOB_CANT_HAVE_DEFAULT:BLOB, TEXT, GEOMETRY or JSON column can't have
    // a default value
    my_core::enum_field_types field_type = field->real_type();
    assert(field_type != MYSQL_TYPE_BLOB && field_type != MYSQL_TYPE_JSON &&
           field_type != MYSQL_TYPE_TEXT);
#endif
    uint32_t size = field->data_length();
    size_t length = 0;
    std::unique_ptr<char[]> value = coder.encode(
        reinterpret_cast<const uchar *>(field->data_ptr()), size, &length);

    dd::String_type default_value;
    default_value.assign(dd::String_type(value.get(), length));
    se_private.set(dd_column_key_strings[DD_INSTANT_COLUMN_DEFAULT],
                   default_value);
  }
#ifdef UNIV_DEBUG
  assert(num_instant_cols > 0);
#endif /* UNIV_DEBUG */
}

/** Look up a column in a table using the system_charset_info collation.
@param[in]	dd_table	data dictionary table
@param[in]	name		column name
@return the column
@retval nullptr if not found */
const dd::Column *dd_find_column(const dd::Table *dd_table, const char *name) {
  for (const dd::Column *c : dd_table->columns()) {
    if (!my_strcasecmp(system_charset_info, c->name().c_str(), name)) {
      return (c);
    }
  }
  return (nullptr);
}

void dd_table_get_instant_default(const dd::Column &column,
                                  uchar **default_value, size_t *len) {
  const dd::Properties &se_private_data = column.se_private_data();
  if (se_private_data.exists(
          dd_column_key_strings[DD_INSTANT_COLUMN_DEFAULT_NULL])) {
    *default_value = nullptr;
    *len = 0;
  } else if (se_private_data.exists(
                 dd_column_key_strings[DD_INSTANT_COLUMN_DEFAULT])) {
    dd::String_type value;
    se_private_data.get(dd_column_key_strings[DD_INSTANT_COLUMN_DEFAULT],
                        &value);
    DD_instant_col_val_coder coder;
    *default_value = coder.decode(value.c_str(), value.length(), len);
  }
}
/** Copy the engine-private parts of column definitions of a table.
@param[in,out]	new_table	Copy of old table
@param[in]	old_table	Old table */
void dd_copy_table_columns(dd::Table &new_table, const dd::Table &old_table) {
  /* Columns in new table maybe more than old tables, when this is
  called for adding instant columns. Also adding and dropping
  virtual columns instantly is another case. */
  for (const auto old_col : old_table.columns()) {
    dd::Column *new_col = const_cast<dd::Column *>(
        dd_find_column(&new_table, old_col->name().c_str()));

    if (new_col == nullptr) {
      assert(old_col->is_virtual());
      continue;
    }

    if (!old_col->se_private_data().empty()) {
      if (!new_col->se_private_data().empty())
        new_col->se_private_data().clear();
      new_col->set_se_private_data(old_col->se_private_data());
    }
  }
}

/** Copy the engine-private parts of a table or partition definition
when the change does not affect RocksDB. This mainly copies the common
private data between dd::Table
@tparam		Table		dd::Table
@param[in,out]	new_table	Copy of old table or partition definition
@param[in]	old_table	Old table or partition definition
@param[in]	copy_old_options Clear options on new table and copy from old */
void dd_copy_private(dd::Table &new_table, const dd::Table &old_table,
                     bool copy_old_options) {
  new_table.se_private_data().clear();
  new_table.set_se_private_id(old_table.se_private_id());
  new_table.set_se_private_data(old_table.se_private_data());
  new_table.table().set_row_format(old_table.table().row_format());
  if (copy_old_options) {
    new_table.options().clear();
    new_table.set_options(old_table.options());
  }
}

/** The encode() will change the byte stream into char stream, by spliting
every byte into two chars, for example, 0xFF, would be split into 0x0F and 0x0F.
So the final storage space would be double.
@param[in]	stream	stream to encode in bytes
@param[in]	in_len	length of the stream
@param[out]	out_len	length of the encoded stream
@return	the encoded stream, which would be destroyed if the unique_ptr
itself is destroyed */
std::unique_ptr<char[]> DD_instant_col_val_coder::encode(const uchar *stream,
                                                         size_t in_len,
                                                         size_t *out_len) {
  char *result = new char[in_len * 2];

  for (size_t i = 0; i < in_len; ++i) {
    uint8_t v1 = ((stream[i] & 0xF0) >> 4);
    uint8_t v2 = (stream[i] & 0x0F);

    result[i * 2] = (v1 < 10 ? '0' + v1 : 'a' + v1 - 10);
    result[i * 2 + 1] = (v2 < 10 ? '0' + v2 : 'a' + v2 - 10);
  }

  *out_len = in_len * 2;

  return std::unique_ptr<char[]>(result);
}

/** The decode() will change the char stream into byte stream, by merging
every two chars into one byte, for example, 0x0F and 0x0F, would be merged into
0xFF.
@param[in]	stream	stream to decode in chars
@param[in]	in_len	length of the stream
@param[out]	out_len	length of the decoded stream
@return	the decoded stream, which would be destroyed by caller */
uchar *DD_instant_col_val_coder::decode(const char *stream, size_t in_len,
                                        size_t *out_len) {
  assert(in_len % 2 == 0);

  uchar *result = reinterpret_cast<uchar *>(
      my_malloc(PSI_NOT_INSTRUMENTED, in_len / 2, MYF(0)));

  for (size_t i = 0; i < in_len / 2; ++i) {
    char c1 = stream[i * 2];
    char c2 = stream[i * 2 + 1];

    assert(isdigit(c1) || (c1 >= 'a' && c1 <= 'f'));
    assert(isdigit(c2) || (c2 >= 'a' && c2 <= 'f'));

    result[i] = ((isdigit(c1) ? c1 - '0' : c1 - 'a' + 10) << 4) +
                ((isdigit(c2) ? c2 - '0' : c2 - 'a' + 10));
  }

  *out_len = in_len / 2;

  return result;
}
}  // namespace myrocks
