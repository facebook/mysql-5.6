/*
   Copyright (c) 2014 MariaDB Corporation Ab

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

#ifdef USE_PRAGMA_INTERFACE
#pragma interface			/* gcc class implementation */
#endif


#include "my_global.h"                   /* ulonglong */
#include "thr_lock.h"                    /* THR_LOCK, THR_LOCK_DATA */
#include "handler.h"                     /* handler */
#include "my_base.h"                     /* ha_rows */

#include "rocksdb/db.h"
#include "rocksdb/cache.h"
#include "rocksdb/comparator.h"
#include "rocksdb/write_batch.h"
#include "rocksdb/statistics.h"

#include "sql_string.h"

#include "rdb_applyiter.h"


/****************************************************************************
 *   Utility functions for debugging RocksDB-SE. Not intended for production use.
 *****************************************************************************/

///////////////////////////////////////////////////////////////////////
// Dump transaction's uncommitted changes into file
///////////////////////////////////////////////////////////////////////

/*
  Print a binary value into file
*/
void dump_value(FILE *out, const rocksdb::Slice &val)
{
  char buf[1000];
  char *p= buf;
  for (size_t i= 0; i < val.size(); i++)
  {
    char *pp= p;
    uint c= (uint)(uchar)val[i];
    DBUG_ASSERT( c < 0x100);
    p += snprintf(p, buf + sizeof(buf) - p, "%02x", c);
    DBUG_ASSERT(p - pp == 2);
    if (p >= buf + sizeof(buf) - 10)
    {
      *p= 0;
      fputs(buf, out);
      p= buf;
    }
  }
  if (p != buf)
  {
    *p= 0;
    fputs(buf, out);
  }
}

/*
  Dump changes for a transaction.

  output format:
    TRX 0x1235
    PUT <binary_key> <binary_value>
    DEL <binary_key>
    ...
*/

void dump_trx_changes(FILE *out, Row_table &changes)
{
  for (int reverse= 0; reverse <= 1; reverse++)
  Row_table_iter iter(&changes, (bool)reverse);
  {
    fprintf(out, "TRX %p\n", current_thd);
    for (iter.SeekToFirst(); iter.Valid(); iter.Next())
    {
      if (iter.is_tombstone())
      {
        fprintf(out, "DEL ");
        dump_value(out, iter.key());
        fputs("\n", out);
      }
      else
      {
        fprintf(out, "PUT ");
        dump_value(out, iter.key());
        fputs(" ", out);
        dump_value(out, iter.value());
        fputs("\n", out);
      }
    }
  }
}

///////////////////////////////////////////////////////////////////////
// Support for reading back the text-based "transaction log"
///////////////////////////////////////////////////////////////////////
class Buffered_reader
{
  char *start_pos;

  FILE *infile;
  char buffer[1000];
  char *end_pos;

  char *buf_end;

public:
  bool is_eof;
  void init(FILE *infile_arg)
  {
    infile= infile_arg;
    end_pos= start_pos= 0;
    is_eof= false;
    buf_end= buffer + sizeof(buffer);
  }

  /* Get data without actually reading it */
  char* peek(int chars)
  {
    if (end_pos - start_pos > chars)
      return start_pos;
    else
    {
      if (is_eof)
        return NULL;
      else
      {
        /* Read more data from the file */
        size_t size= end_pos - start_pos;
        memmove(buffer, start_pos, size);
        start_pos= buffer;
        end_pos=   buffer + size;
        size_t res= fread(end_pos, 1, buf_end - end_pos, infile);
        if ( res < buf_end - end_pos)
          is_eof= true;
        end_pos += res;

        /* Try peeking again */
        if (end_pos - start_pos > chars)
          return start_pos;
        else
          return NULL;
      }
    }
  }

  void read(int chars)
  {
    DBUG_ASSERT(end_pos - start_pos >= chars);
    start_pos += chars;
  }

  void get_binary(std::string *strbuf)
  {
    char *p;
    char val;
    strbuf->clear();
    while ((p= peek(2)))
    {
      if (p[0] >='0' && p[0] <= '9')
        val = 0x10 * (p[0] - '0');
      else if (p[0] >= 'a' && p[0] <='f')
        val = 0x10 * (p[0] - 'a'+ 10 );
      else
        return;

      if (p[1] >='0' && p[1] <= '9')
        val += (p[1] - '0');
      else if (p[1] >= 'a' && p[1] <='f')
        val += (p[1] - 'a' + 10);
      else
        return;

      read(2);
      strbuf->push_back(val);
    }
  }

  void next_line()
  {
    char *p;
    while ((p= peek(1)))
    {
      if (*p =='\n')
      {
        read(1);
        break;
      }
      read(1);
    }
  }

  bool skip_char(char c)
  {
    char *p;
    if (!(p= peek(1)) ||
        *p != c)
      return true; // failed
    read(1);
    return false; // ok
  }
};

/*
  @return false - OK, read and processed...
  @return true - Error or eof
*/
bool read_trx(Buffered_reader *reader, bool *error)
{
  char *p;
  *error= true;

  rocksdb::WriteBatch batch;

  if (!(p= reader->peek(4)))
  {
    *error= false; // Not enough for header, assume eof
    return true;
  }
  if (strncmp(p, "TRX ", 4))
    return true;
  reader->read(4);
  reader->next_line();

  //printf("TRX\n");
  while ((p= reader->peek(4)))
  {
    bool is_put= !strncmp(p, "PUT ", 4);
    bool is_del= !strncmp(p, "DEL ", 4);
    if (!is_put && !is_del)
    {
      if (!strncmp(p, "TRX ", 4))
        break;
      return true;
    }

    reader->read(4);

    std::string key;
    std::string value;
    reader->get_binary(&key);

    if (is_put)
    {
      if (reader->skip_char(' '))
        return true;

      reader->get_binary(&value);
    }

    if (!reader->is_eof && reader->skip_char('\n'))
      return true;

    if (is_put)
    {
      //printf("  PUT key=#%ld, val=#%ld\n", key.size(), value.size());
      batch.Put(rocksdb::Slice(key.data(),  key.size()),
                rocksdb::Slice(value.data(), value.size()));
    }
    else
    {
      //printf("  DEL key=#%ld\n", key.size());
      batch.Delete(rocksdb::Slice(key.data(), key.size()));
    }
  }
  rocksdb::Status s= rdb->Write(rocksdb::WriteOptions(), &batch);
  if (!s.ok())
  {
    fprintf(stderr, "Error applying the transaction\n");
    return true;
  }
  return false;
}

void apply_and_check(const char *filename, ha_rocksdb *h)
{
  FILE *in= fopen(filename,"r");
  Buffered_reader reader;
  reader.init(in);
  bool eof;
  int cnt=0;
  HA_CHECK_OPT opt;


  if (h->check(current_thd, &opt) != HA_ADMIN_OK)
  {
    fprintf(stderr, "Check at initial state failed\n");
    return;
  }

  while (!read_trx(&reader, &eof))
  {
    if (h->check(current_thd, &opt) != HA_ADMIN_OK)
    {
      fprintf(stderr, "TRX #%d: failed\n", cnt);
      break;
    }
    fprintf(stderr, "TRX #%d: OK\n", cnt);
    cnt++;
  }
  fprintf(stderr, "end, eof=%d\n", (int)eof);
}

///////////////////////////////////////////////////////////////////////
// Dump per-row operations
///////////////////////////////////////////////////////////////////////

void dump_value(FILE *out, const rocksdb::Slice &val);

static void dump_put_cmd(rocksdb::Slice &key, rocksdb::Slice &value)
{
#if 0
  FILE *out= trx_dump_file;
  mysql_mutex_lock(&write_mutex);

  fprintf(out, "TRX %p PUT ", current_thd);

  dump_value(out, key);
  fputs(" ", out);
  dump_value(out, value);
  fputs("\n", out);

  mysql_mutex_unlock(&write_mutex);
#endif
}

static void dump_delete_cmd(rocksdb::Slice &key)
{
#if 0
  mysql_mutex_lock(&write_mutex);
  FILE *out= trx_dump_file;
  fprintf(out, "TRX %p DEL ", current_thd);
  dump_value(out, key);
  fputs("\n", out);

  mysql_mutex_unlock(&write_mutex);
#endif
}
