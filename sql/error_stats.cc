#include "sql_base.h"
#include "sql_show.h"
#include "my_atomic.h"
#include "my_stacktrace.h"
#include "mysqld.h"
#include "errmsg.h"
#include <mysqld_error.h> // error names
#include <utility>

static const uint num_sections = array_elements(errmsg_section_size);

typedef std::pair<ulonglong, atomic_stat<ulonglong>> time_counter_pair;
time_counter_pair global_error_stats[errmsg_total_count+1];

/* List of error names and error codes */
typedef struct
{
  const char *name;
  uint        code;
  const char *text;
} st_error;

static const st_error error_names[] =
{
  { "ER_CUSTOM", 0, "" }, //use 0 slot since system errors should be > 0
#include <mysqld_ername.h>
};

static_assert(array_elements(error_names) == errmsg_total_count + 1,
              "error_names and errmsg_total_count out of sync");

void init_global_error_stats(void)
{
  // Checks that error_names array is well-formed.
  uint k = 1;
  for (uint i = 0; i < num_sections; i++) {
    for (uint j = 0; j < (uint)errmsg_section_size[i]; j++) {
      if (k >= array_elements(error_names) ||
          error_names[k].code != errmsg_section_start[i] + j) {
        abort();
      }
      k++;
    }
  }
}

void reset_global_error_stats(void)
{
  memset(global_error_stats, 0, sizeof(global_error_stats));
}

void update_error_stats(uint error)
{
  uint offset = 1;
  for (uint i = 0; i < num_sections; i++) {
    if (error >= (uint)errmsg_section_start[i] &&
        error < (uint)errmsg_section_start[i] + errmsg_section_size[i]) {
      offset += (error - errmsg_section_start[i]);
      break;
    } else {
      offset += errmsg_section_size[i];
    }
  }

  // If error is not known, then it is a custom error. Put it in the 0th slot.
  DBUG_ASSERT(offset <= array_elements(error_names));
  if (offset >= array_elements(error_names)) {
    offset = 0;
  }

  time_counter_pair &tc = global_error_stats[offset];
  tc.first = my_getsystime();
  tc.second.inc();
}

int fill_error_stats(THD *thd, TABLE_LIST *tables, Item *cond)
{
  DBUG_ENTER("fill_error_stats");
  TABLE* table= tables->table;
  unsigned f;
  const char *err_name = NULL;

  for (uint error = 0; error < array_elements(global_error_stats); ++error) {
    time_counter_pair &stat = global_error_stats[error];
    if (!stat.first) {
      continue;
    }

    restore_record(table, s->default_values);
    f = 0;

    table->field[f++]->store(error_names[error].code);

    err_name = error_names[error].name;
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
