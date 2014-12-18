/*
   Copyright (c) 2013 Monty Program Ab

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

#include "my_global.h"                   /* ulonglong */
#include "my_base.h"                     /* ha_rows */
#include "my_sys.h"
#include "my_tree.h"

#include "rocksdb/db.h"
#include "rocksdb/comparator.h"
#include "rocksdb/write_batch.h"

#include "rdb_rowmods.h"
#include "rdb_applyiter.h"

int compare_mem_comparable_keys(const uchar *a, size_t a_len, const uchar *b, size_t b_len);


Apply_changes_iter::Apply_changes_iter() :
  trx(NULL), rdb(NULL)
{}


Apply_changes_iter::~Apply_changes_iter()
{
  delete trx;
  delete rdb;
}


void Apply_changes_iter::init(bool is_reverse_arg, Row_table *trx_arg,
                              rocksdb::Iterator *rdb_arg)
{
  delete trx;
  delete rdb;
  is_reverse= is_reverse_arg;
  trx= new Row_table_iter(trx_arg, is_reverse);
  rdb= rdb_arg;
  valid= false;
}


void Apply_changes_iter::Next()
{
  DBUG_ASSERT(valid);
  if (cur_is_trx)
    trx->Next();
  else
    rdb->Next();

  advance(1);
}


void Apply_changes_iter::Prev()
{
  DBUG_ASSERT(valid);
  if (cur_is_trx)
    trx->Prev();
  else
    rdb->Prev();

  advance(-1);
}


void Apply_changes_iter::Seek(rocksdb::Slice &key)
{
  rdb->Seek(key);
  trx->Seek(key);
  advance(1);
}


void Apply_changes_iter::SeekToLast()
{
  rdb->SeekToLast();
  trx->SeekToLast();
  advance(-1);
}


/*
  @param direction  1 means forward, -1 means backward.
*/

void Apply_changes_iter::advance(int direction)
{
  valid= true;
  while (1)
  {
    if (!trx->Valid() && !rdb->Valid())
    {
      // ok we got here if neither scan nor trx have any records.
      cur_is_trx= false;  //just set it to something
      valid= false;
      return;
    }

    if (!trx->Valid())
    {
      /* Got record from rocksdb but not from trx */
      cur_is_trx= false;
      break;
    }

    if (!rdb->Valid())
    {
      cur_is_trx= true;
      if (trx->is_tombstone())
      {
        if (direction == 1)
          trx->Next();
        else
          trx->Prev();
        continue;  /* A tombstone.. (but no matching record? odd..) */
      }
      break;
    }

    if (rdb->Valid() && trx->Valid())
    {
      rocksdb::Slice rdb_key= rdb->key();
      rocksdb::Slice trx_key= trx->key();
      int cmp= direction *
               compare_mem_comparable_keys((const uchar*)trx_key.data(), trx_key.size(),
                                           (const uchar*)rdb_key.data(), rdb_key.size());
      if (is_reverse)
        cmp *= -1;

      if (!cmp) // keys are equal
      {
        if (trx->is_tombstone())
        {
          /* rocksdb has a record, but trx says we have deleted it */
          if (direction == 1)
          {
            rdb->Next();
            trx->Next();
          }
          else
          {
            rdb->Prev();
            trx->Prev();
          }
          continue;  // restart the logic
        }

        /* trx has a newer version of the record */
        if (direction == 1)
          rdb->Next();
        else
          rdb->Prev();
        cur_is_trx= true;
        break;
      }
      else if (cmp > 0)
      {
        /* record from rocksdb comes first */
        cur_is_trx= false;
        break;
      }
      else // cmp < 0
      {
        /* record from transaction comes first */
        if (trx->is_tombstone())
        {
          if (direction == 1)
            trx->Next();
          else
            trx->Prev();
          continue;  /* A tombstone.. (but no matching record? odd..) */
        }
        /* A record from transaction but not in the db */
        cur_is_trx= true;
        break;
      }
    }
  }
}


rocksdb::Slice Apply_changes_iter::value()
{
  if (cur_is_trx)
    return trx->value();
  else
    return rdb->value();
}


rocksdb::Slice Apply_changes_iter::key()
{
  if (cur_is_trx)
    return trx->key();
  else
    return rdb->key();
}
