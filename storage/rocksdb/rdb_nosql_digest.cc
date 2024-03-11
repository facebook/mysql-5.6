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
