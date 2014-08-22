#ifndef ITEM_JSONFUNC_INCLUDED
#define ITEM_JSONFUNC_INCLUDED

/* Copyright (c) 2014, Facebook. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
*/


/* This file defines all json functions */

class Item_func_json_valid :public Item_bool_func
{
public:
  Item_func_json_valid(Item *a) :Item_bool_func(a) {}
  const char *func_name() const { return "json_valid"; }
  bool val_bool();
  longlong val_int();
};

class Item_func_json_extract :public Item_str_func
{
public:
  Item_func_json_extract(List<Item> &list) :Item_str_func(list) { }
  Item_func_json_extract(Item *a,Item *b) :Item_str_func(a,b) {}
  const char *func_name() const { return "json_extract"; }
  String *val_str(String *);
  void fix_length_and_dec();
};

class Item_func_json_contains_key :public Item_bool_func
{
public:
  Item_func_json_contains_key(List<Item> &list) :Item_bool_func(list) { }
  Item_func_json_contains_key(Item *a,Item *b) :Item_bool_func(a,b) {}
  const char *func_name() const { return "json_contains_key"; }
  bool val_bool();
  longlong val_int();
};

class Item_func_json_array_length :public Item_int_func
{
public:
  Item_func_json_array_length(Item *a) :Item_int_func(a) {}
  const char *func_name() const { return "json_array_length"; }
  longlong val_int();
  void fix_length_and_dec() { max_length=21; }
};

#endif /* ITEM_JSONFUNC_INCLUDED */
