/*
   Copyright (c) 2016, Facebook. All rights reserved.

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


/* This file defines all lua string functions */


/* May include caustic 3rd-party defs. Use early, so it can override nothing. */
#include "my_global.h"

/*
  It is necessary to include set_var.h instead of item.h because there
  are dependencies on include order for set_var.h and item.h. This
  will be resolved later.
*/
#include "sql_class.h"
#include "set_var.h"
#include "mysqld.h"
#include "item_cmpfunc.h"


String *Item_func_lua_call::val_str(String *str)
{
  String buf;
  for (unsigned i = 0; i < arg_count; ++i)
  {
    String *p = args[i]->val_str(&buf);
    if (p == NULL)
      break;
  }

  const char *ret = "Lua Call";
  str->copy(ret, strlen(ret), collation.collation);
  return str;
}

void Item_func_lua_call::fix_length_and_dec()
{
  // use the lua data size (first arg)
  ulonglong char_length= args[0]->max_char_length();
  fix_char_length_ulonglong(char_length);
}
