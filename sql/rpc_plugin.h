#ifndef RPC_PLUGIN_INCLUDED
#define RPC_PLUGIN_INCLUDED

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// These structs are defined for communication between thrift plugin and myrocks
typedef enum {
  BOOL = 0,
  UNSIGNED_INT = 1,
  SIGNED_INT = 2,
  DOUBLE = 3,
  STRING = 4
} myrocks_value_type;

struct myrocks_column_cond_value {
  myrocks_value_type type;
  uint32_t length = 0;  // valid for string
  union {
    bool boolVal;
    uint64_t i64Val;
    double doubleVal;
    const char *stringVal;
  };
};

struct myrocks_where_item {
  typedef enum {
    EQ = 0,
    LT = 1,
    GT = 2,
    LE = 3,
    GE = 4,
  } where_op;

  std::string column;
  where_op op;
  myrocks_column_cond_value value;
};

struct myrocks_order_by_item {
  typedef enum {
    ASC = 0,
    DESC = 1,
  } order_by_op;

  std::string column;
  order_by_op op;
};

struct myrocks_column_value {
  myrocks_value_type type;
  bool isNull = false;
  uint32_t length = 0;  // valid for string and binary
  union {
    bool boolVal;
    uint64_t i64Val;
    int64_t signed_i64Val;
    double doubleVal;
    uchar *stringVal;
  };
};

static const int MAX_VALUES_PER_RPC_WHERE_ITEM = 30;
struct myrocks_where_in_item {
  std::string column;
  std::array<myrocks_column_cond_value, MAX_VALUES_PER_RPC_WHERE_ITEM> values;
  // If the number of values is above the limit, then these values are stored in
  // the following more_values.
  myrocks_column_cond_value *more_values;
  uint32_t num_values;
};

// This rpc buffer is allocated per each thrift io thread
const int MAX_COLUMNS_PER_RPC_BUFFER = 200;
using myrocks_columns =
    std::array<myrocks_column_value, MAX_COLUMNS_PER_RPC_BUFFER>;

/*
   myrocks calls this callback function whenever a row is ready.
   rpc_buffer             : points to protocol-specific (eg. thrift) rpc buffer
                            where myrocks_columns are converted to
                            protocol-specific format and stored.
   myrocks_columns_values : points to where myrocks_column_value typed values
                            are stored.
   num_columns            : stores the number of columns to be fetched from
                            myrocks_column_values.
*/
using myrocks_bypass_rpc_send_row_fn =
    void (*)(void *rpc_buffer, myrocks_columns *myrocks_columns_values,
             uint64_t num_columns);

// This is allocated and populated by thrift plugin
struct myrocks_select_from_rpc {
  std::string db_name;
  std::string table_name;
  std::vector<std::string> columns;
  std::vector<myrocks_where_item> where;
  std::vector<myrocks_where_in_item> where_in;
  uint64_t limit;
  uint64_t limit_offset;
  std::string force_index;
  std::vector<myrocks_order_by_item> order_by;
  void *rpc_buffer;
  myrocks_bypass_rpc_send_row_fn send_row;
};

struct bypass_rpc_exception {
  uint32_t errnum = 0;
  std::string sqlstate;
  std::string message;
};

#endif /* RPC_PLUGIN_INCLUDED */
