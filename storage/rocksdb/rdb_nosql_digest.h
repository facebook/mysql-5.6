/* C++ standard header files */
#include <sys/types.h>

/* MySQL header files */
#include "./lex_string.h"
#include "./m_string.h"
#include "sql/item_func.h"
#include "sql/sql_digest_stream.h"

#pragma once

/*
  This file contains contains variables and functions that are used for digest
  computation in nosql layer.
*/

namespace myrocks {

static LEX_CSTRING select_tok{STRING_WITH_LEN("SELECT")};
static LEX_CSTRING from_tok{STRING_WITH_LEN("FROM")};
static LEX_CSTRING where_tok{STRING_WITH_LEN("WHERE")};
static LEX_CSTRING force_tok{STRING_WITH_LEN("FORCE")};
static LEX_CSTRING index_tok{STRING_WITH_LEN("INDEX")};
static LEX_CSTRING order_tok{STRING_WITH_LEN("ORDER")};
static LEX_CSTRING by_tok{STRING_WITH_LEN("BY")};
static LEX_CSTRING limit_tok{STRING_WITH_LEN("LIMIT")};
static LEX_CSTRING asc_tok{STRING_WITH_LEN("ASC")};
static LEX_CSTRING desc_tok{STRING_WITH_LEN("DESC")};

static LEX_CSTRING eq_tok{STRING_WITH_LEN("=")};
static LEX_CSTRING lt_tok{STRING_WITH_LEN("<")};
static LEX_CSTRING gt_tok{STRING_WITH_LEN(">")};
static LEX_CSTRING le_tok{STRING_WITH_LEN("<=")};
static LEX_CSTRING ge_tok{STRING_WITH_LEN(">=")};

// This should always be synced with rocksdb::convert_where_op
inline LEX_CSTRING get_op_lex_string(Item_func::Functype op) {
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
