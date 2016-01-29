#ifndef ITEM_LUAFUNC_INCLUDED
#define ITEM_LUAFUNC_INCLUDED

/* Copyright (c) 2016, Facebook. All rights reserved.

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


/* This file defines all lua functions */

class Item_func_lua_call :public Item_str_func
{
public:
  Item_func_lua_call(List<Item> &list) :Item_str_func(list) { }
  Item_func_lua_call(Item *a,Item *b) :Item_str_func(a,b) {}
  const char *func_name() const { return "lua_call"; }
  String *val_str(String *);
  void fix_length_and_dec();
  virtual enum Functype functype() const   { return LUA_CALL_FUNC; }

protected:
  String *intern_val_str(String *str, bool val_only);
};


#endif /* ITEM_LUAFUNC_INCLUDED */
