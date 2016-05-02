/*
   Copyright (c) 2016, Facebook, Inc.

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

/* C++ header files */
#include <string>

/* MySQL header files */
#include "./sql_string.h"

namespace myrocks {

/*
  Guess what?
  An interface is a class where all members are public by default.
*/

#ifndef interface
#define interface struct
#endif  // interface

/*
  Introduce C-style pseudo-namespaces, a handy way to make code more readble
  when calling into a legacy API, which does not have any namespace defined.
  Since we cannot or don't want to change the API in any way, we can use this
  mechanism to define readability tokens that look like C++ namespaces, but are
  not enforced in any way by the compiler, since the pre-compiler strips them
  out. However, on the calling side, code looks like my_core::thd_ha_data()
  rather than plain a thd_ha_data() call. This technique adds an immediate
  visible cue on what type of API we are calling into.
*/

#ifndef my_core
// C-style pseudo-namespace for MySQL Core API, to be used in decorating calls
// to non-obvious MySQL functions, like the ones that do not start with well
// known prefixes: "my_", "sql_", and "mysql_".
#define my_core
#endif  // my_core

/*
  Helper function to get an uchar* out of a MySQL String.
*/

inline uchar* rdb_str_to_uchar_ptr(String * str)
{
  DBUG_ASSERT(str != nullptr);
  return reinterpret_cast<uchar*>(str->c_ptr());
}

/*
  Helper function to parse strings
*/
const char* rdb_skip_spaces(struct charset_info_st* cs, const char *str)
  __attribute__((__nonnull__, __warn_unused_result__));

bool rdb_compare_strings_ic(const char *str1, const char *str2)
  __attribute__((__nonnull__, __warn_unused_result__));

const char* rdb_find_in_string(const char *str, const char *pattern,
                               bool *succeeded)
  __attribute__((__nonnull__, __warn_unused_result__));

const char* rdb_check_next_token(struct charset_info_st* cs, const char *str,
                                 const char *pattern, bool *succeeded)
  __attribute__((__nonnull__, __warn_unused_result__));

const char* rdb_parse_id(struct charset_info_st* cs, const char *str,
                         std::string *id)
  __attribute__((__nonnull__(1, 2), __warn_unused_result__));

const char* rdb_skip_id(struct charset_info_st* cs, const char *str)
  __attribute__((__nonnull__, __warn_unused_result__));

std::string rdb_hexdump(const char *data, std::size_t data_len,
                        std::size_t maxsize = 0)
  __attribute__((__nonnull__));
}  // namespace myrocks
