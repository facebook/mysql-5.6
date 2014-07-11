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

#include "leveldb/db.h"
#include "leveldb/comparator.h"
#include "leveldb/write_batch.h"

#include "ldb_rowmods.h"
#include "ldb_applyiter.h"

int compare_mem_comparable_keys(const uchar *a, size_t a_len, const uchar *b, size_t b_len);


Apply_changes_iter::Apply_changes_iter() :
  trx(NULL), ldb(NULL)
{}


Apply_changes_iter::~Apply_changes_iter()
{
  delete trx;
  delete ldb;
}


void Apply_changes_iter::init(Row_table *trx_arg, leveldb::Iterator *ldb_arg)
{
  delete trx;
  delete ldb;
  trx= new Row_table_iter(trx_arg);
  ldb= ldb_arg;
  valid= false;
}


void Apply_changes_iter::Next()
{
  DBUG_ASSERT(valid);
  if (cur_is_trx)
    trx->Next();
  else
    ldb->Next();

  advance(1);
}


void Apply_changes_iter::Prev()
{
  DBUG_ASSERT(valid);
  if (cur_is_trx)
    trx->Prev();
  else
    ldb->Prev();

  advance(-1);
}


void Apply_changes_iter::Seek(leveldb::Slice &key)
{
  ldb->Seek(key);
  trx->Seek(key);
  advance(1);
}


void Apply_changes_iter::SeekToLast()
{
  ldb->SeekToLast();
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
    if (!trx->Valid() && !ldb->Valid())
    {
      // ok we got here if neither scan nor trx have any records.
      cur_is_trx= false;  //just set it to something
      valid= false;
      return;
    }

    if (!trx->Valid())
    {
      /* Got record from leveldb but not from trx */
      cur_is_trx= false;
      break;
    }

    if (!ldb->Valid())
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

    if (ldb->Valid() && trx->Valid())
    {
      leveldb::Slice ldb_key= ldb->key();
      leveldb::Slice trx_key= trx->key();
      int cmp= direction *
               compare_mem_comparable_keys((const uchar*)trx_key.data(), trx_key.size(),
                                           (const uchar*)ldb_key.data(), ldb_key.size());
      if (!cmp) // keys are equal
      {
        if (trx->is_tombstone())
        {
          /* leveldb has a record, but trx says we have deleted it */
          if (direction == 1)
          {
            ldb->Next();
            trx->Next();
          }
          else
          {
            ldb->Prev();
            trx->Prev();
          }
          continue;  // restart the logic
        }

        /* trx has a newer version of the record */
        if (direction == 1)
          ldb->Next();
        else
          ldb->Prev();
        cur_is_trx= true;
        break;
      }
      else if (cmp > 0)
      {
        /* record from leveldb comes first */
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


leveldb::Slice Apply_changes_iter::value()
{
  if (cur_is_trx)
    return trx->value();
  else
    return ldb->value();
}


leveldb::Slice Apply_changes_iter::key()
{
  if (cur_is_trx)
    return trx->key();
  else
    return ldb->key();
}
