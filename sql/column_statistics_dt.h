#pragma once

#include <array>
#include <string>

#include "sql_digest.h"

using md5_key = std::array<unsigned char, MD5_HASH_SIZE>;

/*
 * `sql_operation` defines operations performed by a query where an index could
 * have been useful.
 */
enum sql_operation: int {
  UPDATE,
  FILTER,
  TABLE_JOIN,
  ORDER_BY
};

/*
 * `operator_type` defines operators which let specify filtration predicates
 * across a variety of queries - update, select, delete etc.
 */
enum operator_type: int {
  EQUAL,
  LESS_THAN,
  LESS_THAN_EQUAL,
  GREATER_THAN,
  GREATER_THAN_EQUAL,
  NOT_EQUAL,
  SET_MEMBERSHIP,
  PATTERN_MATCH,
  UNKNOWN_OPERATOR
};

/*
 * ColumnUsageInfo struct contains information about the usage of a specific
 * column in a table. A single query may use several different columns across
 * multiple tables.
 */
struct ColumnUsageInfo {
  std::string table_schema;
  std::string table_name;
  std::string column_name;
  sql_operation sql_op;
  operator_type op_type;

  // Comparator required for defining a strict weak ordering of
  // `ColumnUsageInfo` structs.
  bool operator<(const ColumnUsageInfo&) const;
};
