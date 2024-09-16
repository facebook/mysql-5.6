/*
   Copyright (c) Meta Platforms, Inc. and affiliates.

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

#include "./rdb_nosql_digest.h"

/* MySQL header files */
#include "sql/sql_yacc.h"

namespace myrocks {

uint nosql_ident_token() {
  return IDENT;
}

uint nosql_decimal_num_token() {
  return DECIMAL_NUM;
}

uint nosql_text_string_token() {
  return TEXT_STRING;
}

}  // namespace myrocks
