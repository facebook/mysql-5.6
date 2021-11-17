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

static const char *log_filename = "test_get_column_ref_info";
FILE *outfile;

static void create_log_file(const char *log_name) {
  char filename[FN_REFLEN];

  fn_format(filename, log_name, "", ".log",
            MY_REPLACE_EXT | MY_UNPACK_FILENAME);
  unlink(filename);
  outfile = my_fopen(filename, O_CREAT | O_RDWR, MYF(0));
}

class Test_column_ref_info_visitor : public Select_lex_visitor {
 public:
  Test_column_ref_info_visitor() {}
  bool visit_union(Query_expression *) override { return false; }
  bool visit_query_block(Query_block *) override { return false; }
  bool visit_item(Item *item) override {
    Column_ref_info cri;
    if (get_column_ref_info(item, cri)) {
      fprintf(outfile, "Column_ref_info db = (%s), table (%s), field (%s)\n",
              cri.m_db_name.c_str(), cri.m_table_name.c_str(),
              cri.m_column_name.c_str());
    }
    return false;
  }

  ~Test_column_ref_info_visitor() override {}
};

/*
  @brief audit notify function to test get_column_ref_info

  @param [in] thd         Connection context.
  @param [in] event_class Event class value.
  @param [in] event       Event data.

  @retval Value indicating, whether the server should abort continuation
          of the current operation.
*/
static int test_get_column_ref_info_notify(THD *thd,
                                           mysql_event_class_t event_class,
                                           const void *event) {
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
    Test_column_ref_info_visitor visitor;
    thd->lex->unit->accept(&visitor);
    fprintf(outfile, "\n");
    fflush(outfile);
  }
  return false;
}

static int test_get_column_ref_info_plugin_init(void *) {
  DBUG_TRACE;
  create_log_file(log_filename);
  fprintf(outfile, "Plugin test plugin initialized\n");
  return 0;
}

static int test_get_column_ref_info_plugin_deinit(void *) {
  my_fclose(outfile, MYF(0));
  DBUG_TRACE;
  return 0;
}

/* Mandatory structure describing the properties of the plugin. */
static struct st_mysql_audit test_get_column_ref_info_plugin = {
    MYSQL_AUDIT_INTERFACE_VERSION,   /* interface version */
    nullptr,                         /* release_thd function */
    test_get_column_ref_info_notify, /* notify function */
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
    &test_get_column_ref_info_plugin,
    "test_get_column_ref_info",
    "MySQL Eng",
    "Test get_column_ref_info",
    PLUGIN_LICENSE_GPL,
    test_get_column_ref_info_plugin_init,   /* Plugin Init */
    nullptr,                                /* Plugin Check uninstall */
    test_get_column_ref_info_plugin_deinit, /* Plugin Deinit */
    0x0100 /* 1.0 */,
    nullptr, /* status variables                */
    nullptr, /* system variables                */
    nullptr, /* config options                  */
    0,       /* flags                           */
} mysql_declare_plugin_end;
