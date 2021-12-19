/* Copyright (c) 2015, 2020, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.

   This program is also distributed with certain software (including
   but not limited to OpenSSL) that is licensed under separate terms,
   as designated in a particular file or component or in included license
   documentation.  The authors of MySQL hereby grant you an additional
   permission to link the program and your derivative works with the
   separately licensed software that they have included with MySQL.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#include <fcntl.h>
#include <mysql/plugin.h>
#include <mysql/plugin_audit.h>
#include <mysql/service_privacy.h>
#include <mysql_version.h>
#include <mysqld_error.h>
#include <sql/select_lex_visitor.h>
#include <sql/sql_class.h>  // THD
#include <sql/sql_lex.h>
#include <sql/sql_thd_internal_api.h>
#include "m_string.h"  // strlen
#include "my_dbug.h"
#include "my_inttypes.h"
#include "my_io.h"
#include "my_sys.h"  // my_write, my_malloc

static const char *log_filename = "test_build_column_lineage_info";
FILE *outfile;

static void create_log_file(const char *log_name) {
  char filename[FN_REFLEN];

  fn_format(filename, log_name, "", ".log",
            MY_REPLACE_EXT | MY_UNPACK_FILENAME);
  unlink(filename);
  outfile = my_fopen(filename, O_CREAT | O_RDWR, MYF(0));
}

/**
 * Traverse the column lineage info recursively and print out the contents with
 * DBUG. In MTR, the trace file enabled a MTR test to verify against the
 * expected trace output, ensuring the lineage info is constructed as expected.
 */
void validate_column_lineage_info(Column_lineage_info *cli, int32_t level) {
  DBUG_TRACE;

  if (!cli) return;
  DBUG_PRINT("column_lineage_info", ("level %d id %d", level, cli->m_id));

  switch (cli->type()) {
    case Column_lineage_info::Type::UNION: {
      DBUG_PRINT("column_lineage_info", ("type: UNIT"));
      Union_column_lineage_info *union_cli = (Union_column_lineage_info *)cli;

      // traverse next level
      for (const auto &parent : union_cli->m_parents) {
        validate_column_lineage_info(parent, level + 1);
      }
      break;
    }
    case Column_lineage_info::Type::QUERY_BLOCK: {
      DBUG_PRINT("column_lineage_info", ("type: SELECT_LEX"));

      Query_block_column_lineage_info *query_block_cli =
          (Query_block_column_lineage_info *)cli;
      int32_t field_id [[maybe_unused]] = 0;
      for (const auto &field_lineage_info : query_block_cli->m_selected_field) {
        DBUG_PRINT("column_lineage_info", ("Field %d", field_id));
        for (const auto &item_lineage_info :
             field_lineage_info.m_item_lineage_info) {
          if (item_lineage_info.m_cli == nullptr) {
            DBUG_PRINT(
                "column_lineage_info",
                ("Item source lineage info for field (%d) is NULL unexpectedly",
                 field_id));
          } else {
            DBUG_PRINT(
                "column_lineage_info",
                ("Item lineage: index (%d), source lineage info (%d) => "
                 "dest field (%s)",
                 item_lineage_info.m_index, item_lineage_info.m_cli->m_id,
                 field_lineage_info.m_field_name.c_str()));
          }
        }
        field_id++;
      }

      // traverse next level
      for (const auto &parent : query_block_cli->m_parents) {
        validate_column_lineage_info(parent, level + 1);
      }
      break;
    }
    case Column_lineage_info::Type::TABLE: {
      Table_column_lineage_info *table_cli = (Table_column_lineage_info *)cli;
      DBUG_PRINT("column_lineage_info",
                 ("type: TABLE, db_name: %s, table_name: %s, alias_name: %s",
                  table_cli->m_db_name.c_str(), table_cli->m_table_name.c_str(),
                  table_cli->m_table_alias.c_str()));

      int32_t field_id [[maybe_unused]] = 0;
      for (const auto &column : table_cli->m_column_refs) {
        if (column.empty()) {
          DBUG_PRINT("column_lineage_info", ("Column name is empty"));
        }
        DBUG_PRINT("column_lineage_info",
                   ("column: %s, index: %d", column.c_str(), field_id));
        field_id++;
      }
      break;
    }
    case Column_lineage_info::Type::INVALID:
    default: {
      DBUG_PRINT("column_lineage_info",
                 ("Invalid Column_lineage_info type: %d", (int)cli->type()));
      break;
    }
  }
}

/*
  @brief audit notify function to test build_column_lineage_info

  @param [in] thd         Connection context.
  @param [in] event_class Event class value.
  @param [in] event       Event data.

  @retval Value indicating, whether the server should abort continuation
          of the current operation.
*/
static int test_build_column_lineage_info_notify(
    THD *thd, mysql_event_class_t event_class, const void *event) {
  /* return if pointers are null */
  if (!thd || !event) {
    return 0;
  }
  if (event_class != MYSQL_AUDIT_QUERY_CLASS) {
    return 0;
  }
  const struct mysql_event_query *event_query =
      (const struct mysql_event_query *)event;
  if (event_query->event_subclass == MYSQL_AUDIT_QUERY_STMT_PREPARED) {
    std::string query_str;
    thd_query_safe(thd, &query_str);
    fprintf(outfile, "Test query: %s\n", query_str.c_str());
    Column_lineage_info *cli = build_column_lineage_info(thd);
    if (!cli) {
      fprintf(outfile, "build column lineage info failed\n");
    }
    DBUG_SET("+d,column_lineage_info:t");
    DBUG_PRINT("column_lineage_info", ("query: %s", query_str.c_str()));

    // traverse and print out the column lineage info
    validate_column_lineage_info(cli, 0);
    DBUG_PRINT("column_lineage_info", ("\n"));
    DBUG_SET("-d,column_lineage_info:-t");
    fprintf(outfile, "Column_lineage_info %s\n\n",
            cli ? "succeeded" : "failed");
    fflush(outfile);
  }
  return false;
}

static int test_build_column_lineage_info_plugin_init(void *) {
  DBUG_TRACE;
  create_log_file(log_filename);
  fprintf(outfile, "Plugin test plugin initialized\n");
  return 0;
}

static int test_build_column_lineage_info_plugin_deinit(void *) {
  DBUG_TRACE;
  my_fclose(outfile, MYF(0));
  return 0;
}

/* Mandatory structure describing the properties of the plugin. */
static struct st_mysql_audit test_build_column_lineage_info_plugin = {
    MYSQL_AUDIT_INTERFACE_VERSION,         /* interface version */
    nullptr,                               /* release_thd function */
    test_build_column_lineage_info_notify, /* notify function */
    {
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        (unsigned long)MYSQL_AUDIT_QUERY_STMT_PREPARED,
        0,
        0,
        0,
    } /* class mask */
};

mysql_declare_plugin(test_daemon){
    MYSQL_AUDIT_PLUGIN,
    &test_build_column_lineage_info_plugin,
    "test_build_column_lineage_info",
    "MySQL Eng",
    "Test build_column_lineage_info",
    PLUGIN_LICENSE_GPL,
    test_build_column_lineage_info_plugin_init,   /* Plugin Init */
    nullptr,                                      /* Plugin Check uninstall */
    test_build_column_lineage_info_plugin_deinit, /* Plugin Deinit */
    0x0100 /* 1.0 */,
    nullptr, /* status variables */
    nullptr, /* system variables */
    nullptr, /* config options */
    0,       /* flags */
} mysql_declare_plugin_end;
