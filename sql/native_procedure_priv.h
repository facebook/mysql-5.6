/* Copyright (c) 2017, Facebook. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef _native_procedures_priv_h
#define _native_procedures_priv_h

#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>

#include <native_procedure.h>

typedef int proc_t(ExecutionContext* ec, const char *packet, size_t length);

struct native_proc
{
  std::string name;
  std::string dl;
  void *dlhandle;
  proc_t *proc;
  bool enabled;
  std::atomic_ullong count;
  native_proc &operator=(const native_proc &p) {
    name = p.name;
    dl = p.dl;
    dlhandle = p.dlhandle;
    proc = p.proc;
    enabled = p.enabled;
    count = p.count.load();
    return *this;
  }
};

class ExecutionContextImpl final : public ExecutionContext {
public:
  ExecutionContextImpl() :
    m_table(nullptr),
    m_key(nullptr),
    m_keynr(0),
    m_thd(nullptr),
    m_metadata_sent(false),
    m_fatal(false),
    m_asc(0) {
      memset(m_keypart_map, 0, sizeof(m_keypart_map));
      memset(&m_end_key, 0, sizeof(m_end_key));
      memset(m_error_message, 0, sizeof(m_error_message));
    }

  void reset() {
    m_table = nullptr;
    m_key = nullptr;
    m_keynr = 0;
    m_thd = nullptr;
    m_metadata_sent = false;
    m_fatal = false;
    m_asc = 0;
    memset(m_keypart_map, 0, sizeof(m_keypart_map));
    memset(&m_end_key, 0, sizeof(m_end_key));
    memset(m_error_message, 0, sizeof(m_error_message));
    m_columns.resize(0);
    m_fields.resize(0);
  }

  virtual int open_table(const char *db, const char *table);
  virtual int set_read_columns(std::vector<const char *> columns);

  virtual int send_metadata();
  virtual int send_row();

  virtual int index_init(const char *index_name);
  virtual int index_read_map(uint idx, enum ha_rkey_function flag);

  virtual int key_write(uint idx, const std::map<const char *, Value>& keyparts);
  virtual int key_part_count(uint *out);
  virtual int key_part_name(uint index, const char **out);

  virtual int read_range_first(uint range_flag, bool asc);
  virtual int read_range_next();

  virtual int field_is_null(uint index, bool *out);
  virtual int field_val_int(uint index, longlong *out);
  virtual int field_val_real(uint index, double *out);
  virtual int field_val_str(uint index, std::string *out);

  void set_thd(THD *thd);
  const char *set_query(const char *query, size_t len);
  void index_end();

private:
  bool range_ended();
  bool field_covered(const char *name);
  bool check_keyread();

  TABLE_LIST m_tl;
  TABLE *m_table;
  KEY *m_key;
  uint m_keynr;
  THD *m_thd;
  bool m_metadata_sent;
  bool m_fatal;
  int m_asc;

  std::vector<const char*> m_columns;
  std::vector<Field*> m_fields;

  key_part_map m_keypart_map[2];
  std::vector<uchar> m_key_buffer[2];
  key_range m_end_key;

  std::vector<uchar> m_query_buffer;
};

void native_procedure_init();
void native_procedure_destroy();
void native_procedure(THD *thd, const char *packet, size_t length);

int mysql_create_native_procedure(THD *thd, native_proc *np);
int mysql_drop_native_procedure(THD *thd, native_proc *np);

// For information schema.
int fill_native_procs(THD *thd, TABLE_LIST *tables, Item *cond);
extern ST_FIELD_INFO native_procs_fields_info[];
#endif
