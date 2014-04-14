#include "sql_base.h"
#include "sql_show.h"
#include "my_atomic.h"
#include "mysqld.h"
#include "errmsg.h"
#include <mysqld_error.h> // error names
#include <utility>

// The max value of a system/user error is CR_MAX_ERROR
const uint array_size = CR_MAX_ERROR+1;

typedef std::pair<ulonglong, atomic_stat<ulonglong>> time_counter_pair;
time_counter_pair global_error_stats[array_size];

/* List of error names and error codes */
typedef struct
{
  const char *name;
  uint        code;
  const char *text;
} st_error;

static st_error error_names[] =
{
  { "ER_CUSTOM", 0, "" }, //use 0 slot since system errors should be > 0
#include <mysqld_ername.h>
  { 0, 0, 0 }
};

const char* global_error_names[array_size];

void init_global_error_stats(void)
{
  memset(global_error_names, 0, sizeof(const char*)*array_size);
  st_error *pst = error_names;
  while (pst->name) {
    DBUG_ASSERT(pst->code < array_size);

    if (pst->code < array_size) {
      global_error_names[pst->code] = pst->name;
    }

    ++pst;
  }
}

void reset_global_error_stats(void)
{
  memset(global_error_stats, 0, sizeof(time_counter_pair)*array_size);
}

void update_error_stats(uint error)
{
  // If an error is larger than system max error,
  // or we don't have an error symbol (system errors are not all contiguous),
  // it is a custom error. We put it in the 0th slot.
  if (error >= array_size || !global_error_names[error]) {
    error = 0; // custom error all goes to the first slot
  }

  time_counter_pair &tc = global_error_stats[error];
  tc.first = my_getsystime();
  tc.second.inc();
}

int fill_error_stats(THD *thd, TABLE_LIST *tables, Item *cond)
{
  DBUG_ENTER("fill_error_stats");
  TABLE* table= tables->table;
  unsigned f;
  const char *err_name = NULL;

  for (uint error = 0; error < array_size; ++error) {
    time_counter_pair &stat = global_error_stats[error];
    if (!stat.first) {
      continue;
    }

    restore_record(table, s->default_values);
    f = 0;

    table->field[f++]->store(error);

    err_name = global_error_names[error];
    if (!err_name) {
      DBUG_RETURN(-1);
    }

    table->field[f++]->store(err_name, strlen(err_name), &my_charset_latin1);

    table->field[f++]->store(stat.first);
    table->field[f++]->store(stat.second.load());

    if (schema_table_store_record(thd, table)) {
      DBUG_RETURN(-1);
    }
  }

  DBUG_RETURN(0);
}

ST_FIELD_INFO error_stats_fields_info[]=
{
  {"ERROR_CODE", MY_INT32_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ERROR_NAME", MAX_CHAR_WIDTH, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ERROR_LAST_SEEN", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ERRORS_TOTAL", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},

  {0, 0, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE}
};
