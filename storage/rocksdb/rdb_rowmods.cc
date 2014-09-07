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

void Row_table::init()
{
  init_tree(&tree, 512 /*default_alloc_size*/, 0 /*memory_limit*/,
            sizeof(void*)/*size*/, Row_table::compare_rows, 1 /*with_delete*/,
            NULL /*free_element*/, NULL/*custom_arg*/);
  tree.flag |= TREE_NO_DUPS;
  init_alloc_root(&mem_root, 512, 512);
  stmt_id= 1;
  change_id= 0;
}


void Row_table::reinit()
{
  if (tree.elements_in_tree > 0)
  {
    cleanup();
    init();
  }
}


void Row_table::cleanup()
{
  delete_tree(&tree);
  free_root(&mem_root, MYF(0));
}


/*
  This function may
  - find no records with the key.
  - find a record with the key
  - find a tombstone with the key.

  @param found  OUT  TRUE means we've found a record
                     FALSE means we've found a tombstone

  @return true - means we've found a record or a tombstone
          false - means found nothing
*/

bool Row_table::Get(rocksdb::Slice &key, std::string *record, bool *found)
{
  ROW_DATA **row_ptr;
  if ((row_ptr= (ROW_DATA**)tree_search(&tree, &key, &key)))
  {
    ROW_DATA *row= *row_ptr;
    if (row->value_len == DATA_IS_TOMBSTONE)
      *found= false;
    else
    {
      *found= true;
      record->assign(((char*)row) + ROW_DATA_SIZE + row->key_len, row->value_len);
    }
    return true; /* Found either a record or a tombstone */
  }
  else
    return false; /* Not found */
}


int Row_table::compare_rows(const void* arg, const void *a, const void *b)
{
  uchar *pa, *pb;
  size_t a_size, b_size;

  /* One of the parameters may be a rocksdb slice */
  if (a == arg)
  {
    rocksdb::Slice *slice= (rocksdb::Slice*)a;
    pa= (uchar*)slice->data();
    a_size= slice->size();
  }
  else
  {
    ROW_DATA *row = *((ROW_DATA**)a);
    a_size= row->key_len;
    pa= ((uchar*)row) + ROW_DATA_SIZE;
  }

  /* Same as above for b */
  if (b == arg)
  {
    rocksdb::Slice *slice= (rocksdb::Slice*)b;
    pb= (uchar*)slice->data();
    b_size= slice->size();
  }
  else
  {
    ROW_DATA *row = *((ROW_DATA**)b);
    b_size= row->key_len;
    pb= ((uchar*)row) + ROW_DATA_SIZE;
  }

  size_t len= (a_size < b_size) ? a_size : b_size;
  int res= memcmp(pa, pb, len);

  if (!res)
  {
    if (a_size < b_size)
      res= -1;
    if (a_size > b_size)
      res= 1;
  }

  return res;
}

bool Row_table::Put(rocksdb::ColumnFamilyHandle *cf, rocksdb::Slice& key,
                    rocksdb::Slice& val)
{
  uchar *data = (uchar*)alloc_root(&mem_root, ROW_DATA_SIZE + key.size() +
                                              val.size());

  ROW_DATA *rdata= (ROW_DATA*)data;
  rdata->key_len= key.size();
  rdata->value_len= val.size();
  rdata->cf= cf;
  rdata->stmt_id= stmt_id;
  rdata->prev_version= NULL;
  memcpy(data + ROW_DATA_SIZE, key.data(), key.size());
  memcpy(data + ROW_DATA_SIZE + key.size(), val.data(), val.size());

  change_id++;
  if (!tree_insert(&tree, &data, /*key_size*/0, NULL/*custom_arg*/))
  {
    /* There is already a record with this key (or Out-Of-Memory) */
    ROW_DATA **row_ptr;
    row_ptr= (ROW_DATA**)tree_search(&tree, &key, &key);
    if (!row_ptr)
      return true;

    /*
      The record is from a previous statement. We may need to get back to
      that record. Save a pointer to it
    */
    if ((*row_ptr)->stmt_id != stmt_id)
    {
      rdata->prev_version= *row_ptr;
    }
    *row_ptr= rdata;
  }
  return false;
}


/*
  Put a tombstone into the table
*/

bool Row_table::Delete(rocksdb::ColumnFamilyHandle *cf, rocksdb::Slice& key)
{
  uchar *data = (uchar*)alloc_root(&mem_root, ROW_DATA_SIZE + key.size());
  ROW_DATA *rdata= (ROW_DATA*)data;
  rdata->key_len= key.size();
  rdata->value_len= DATA_IS_TOMBSTONE;
  rdata->cf= cf;
  rdata->stmt_id= stmt_id;
  rdata->prev_version= NULL;
  memcpy(data + ROW_DATA_SIZE, key.data(), key.size());

  change_id++;

  if (!tree_insert(&tree, &data, /*key_size*/0, NULL/*custom_arg*/))
  {
    /* There is already a record with this key (or Out-Of-Memory) */
    ROW_DATA **row_ptr;
    row_ptr= (ROW_DATA**)tree_search(&tree, &key, &key);
    if (!row_ptr)
      return true; /* OOM */

    if ((*row_ptr)->stmt_id != stmt_id)
    {
      /*
        The record is from a previous statement. We may need to get back to
        that record. Save a pointer to it
      */
      rdata->prev_version= *row_ptr;
    }

    /* Put the new record instead of the old one */
    *row_ptr= rdata;
  }
  return false;
}


void Row_table::start_stmt()
{
  stmt_id++;
}


/*
  Undo all changes made with the current stmt_id.
*/
void Row_table::rollback_stmt()
{
  ROW_DATA *delete_list= NULL;
  Row_table_iter iter(this);

  /*
    To avoid invalidating the iterator, first collect all items that need to be
    deleted in a linked list, and then actually do the deletes.
  */
  for (iter.SeekToFirst(); iter.Valid(); iter.Next())
  {
    if ((*iter.row_ptr)->stmt_id == stmt_id)
    {
      if ((*iter.row_ptr)->prev_version)
      {
        /*
          This element has a previous version (the previous version is what the
          element was before the current statement).
          Replace the element with the its previous version. They have the same
          key value, so there is no need to re-balance the tree.
        */
        *iter.row_ptr= (*iter.row_ptr)->prev_version;
      }
      else
      {
        /* No previous version. Record for removal */
        (*iter.row_ptr)->prev_version= delete_list;
        delete_list= (*iter.row_ptr);
      }
    }
  }

  /* Do all of the recorded deletes */
  while (delete_list)
  {
    ROW_DATA *next= delete_list->prev_version;

    tree_delete(&tree, &delete_list, /*key_size*/ 0, NULL);

    delete_list= next;
  }

  change_id++;
}


/****************************************************************************
 * Row_table_iter
 ***************************************************************************/

Row_table_iter::Row_table_iter(Row_table *rtable_arg) :
  rtable(rtable_arg), row_ptr(NULL), change_id(rtable_arg->change_id)
{}


void Row_table_iter::Seek(const rocksdb::Slice &slice)
{
  row_ptr= (ROW_DATA**)tree_search_key(&rtable->tree, &slice, parents, &last_pos,
                                       HA_READ_KEY_OR_NEXT, &slice/*custom_arg*/);
  change_id= rtable->change_id;
}


void Row_table_iter::SeekToFirst()
{
  row_ptr= (ROW_DATA**)tree_search_edge(&rtable->tree, parents, &last_pos,
                                        offsetof(TREE_ELEMENT, left));
  change_id= rtable->change_id;
}


void Row_table_iter::SeekToLast()
{
  row_ptr= (ROW_DATA**)tree_search_edge(&rtable->tree, parents, &last_pos,
                                        offsetof(TREE_ELEMENT, right));
  change_id= rtable->change_id;
}


void Row_table_iter::Next()
{
  if (rtable->change_id != change_id)
  {
    change_id= rtable->change_id;
    row_ptr= (ROW_DATA**)tree_search_key(&rtable->tree, row_ptr, parents,
                                         &last_pos, HA_READ_AFTER_KEY,
                                         NULL/*custom_arg*/);
  }
  else
  {
    row_ptr= (ROW_DATA**)tree_search_next(&rtable->tree, &last_pos,
                                          offsetof(TREE_ELEMENT, left),
                                          offsetof(TREE_ELEMENT, right));
  }
}


void Row_table_iter::Prev()
{
  if (rtable->change_id != change_id)
  {
    change_id= rtable->change_id;
    row_ptr= (ROW_DATA**)tree_search_key(&rtable->tree, row_ptr, parents,
                                         &last_pos, HA_READ_BEFORE_KEY,
                                         NULL/*custom_arg*/);
  }
  else
  {
    row_ptr= (ROW_DATA**)tree_search_next(&rtable->tree, &last_pos,
                                          offsetof(TREE_ELEMENT, right),
                                          offsetof(TREE_ELEMENT, left));
  }
}


bool Row_table_iter::Valid()
{
  return (row_ptr != NULL);
}


bool Row_table_iter::is_tombstone()
{
  DBUG_ASSERT(Valid());
  return ((*row_ptr)->value_len == DATA_IS_TOMBSTONE);
}


rocksdb::Slice Row_table_iter::key()
{
  DBUG_ASSERT(Valid());
  return rocksdb::Slice(((char*)*row_ptr) + ROW_DATA_SIZE, (*row_ptr)->key_len);
}


rocksdb::Slice Row_table_iter::value()
{
  DBUG_ASSERT(Valid() && !is_tombstone());
  ROW_DATA *row= *row_ptr;
  return rocksdb::Slice(((char*)row) + ROW_DATA_SIZE + row->key_len,
                        row->value_len);
}


rocksdb::ColumnFamilyHandle *Row_table_iter::cf_handle()
{
  DBUG_ASSERT(Valid());
  ROW_DATA *row= *row_ptr;
  return row->cf;
}
