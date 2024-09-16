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

#pragma once

/* C++ standard header files */
#include <sys/types.h>

/* MySQL header files */
#include "./lex_string.h"
#include "./m_string.h"
#include "sql/item_func.h"

/*
  This file contains contains variables and functions that are used for digest
  computation in nosql layer.
*/

namespace myrocks {

// This should always be synced with rocksdb::convert_where_op
inline LEX_CSTRING get_op_lex_string(Item_func::Functype op) {
  static LEX_CSTRING eq_tok{STRING_WITH_LEN("=")};
  static LEX_CSTRING lt_tok{STRING_WITH_LEN("<")};
  static LEX_CSTRING gt_tok{STRING_WITH_LEN(">")};
  static LEX_CSTRING le_tok{STRING_WITH_LEN("<=")};
  static LEX_CSTRING ge_tok{STRING_WITH_LEN(">=")};

  switch (op) {
    case Item_func::EQ_FUNC:
      return eq_tok;
    case Item_func::LT_FUNC:
      return lt_tok;
    case Item_func::GT_FUNC:
      return gt_tok;
    case Item_func::LE_FUNC:
      return le_tok;
    case Item_func::GE_FUNC:
      return ge_tok;
    default:
      return eq_tok;
  }
}

/*
  Wrappers around token id's found in sql_yacc.h header file. This is needed
  because of macro collisions between sql_yacc.h and rocksdb headers.
*/
uint nosql_ident_token();
uint nosql_decimal_num_token();
uint nosql_text_string_token();

}  // namespace myrocks
