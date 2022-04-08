/* Copyright (c) 2010, 2019, Oracle and/or its affiliates. All rights reserved.

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

#include "sql/sql_info.h"
#include <algorithm>
#include <vector>
#include "include/my_md5.h"
#include "sql/mysqld.h"
#include "sql/sql_class.h"
#include "sql/sql_lex.h"
#include "unordered_map"

inline bool is_sql_stats_collection_above_limit() { return false; }

/***********************************************************************
              Begin - Functions to support SQL findings
************************************************************************/
/*
  SQL_FINDINGS

  Associates a SQL ID with its findings (aka SQL conditions).
*/
const uint sf_max_message_size = 256;  // max message text size
const uint sf_max_query_size = 1024;   // max query text size

/* Global SQL findings map to track findings for all SQL statements */
std::unordered_map<digest_key, SQL_FINDING_VEC> global_sql_findings_map;

ulonglong sql_findings_size = 0;

/*
  free_global_sql_findings
    Frees global_sql_findings
*/
void free_global_sql_findings(void) {
  mysql_mutex_lock(&LOCK_global_sql_findings);

  for (auto &finding_iter : global_sql_findings_map)
    finding_iter.second.clear();

  global_sql_findings_map.clear();
  sql_findings_size = 0;

  mysql_mutex_unlock(&LOCK_global_sql_findings);
}

/*
  populate_sql_findings
    Populate the findings for the SQL statement that just ended into
    the specificed finding map

  Input:
    thd         in:  - THD
    query_text  in:  - text of the SQL statement
    finding_vec out: - vector that stores the findings of the statement
                       (key is the warning code)
*/
static void populate_sql_findings(THD *thd, char *query_text,
                                  SQL_FINDING_VEC &finding_vec) {
  Diagnostics_area::Sql_condition_iterator it =
      thd->get_stmt_da()->sql_conditions();

  const Sql_condition *err;
  while ((err = it++)) {
    ulonglong now = my_getsystime() / 10000000;
    const uint err_no = err->mysql_errno();

    // Lookup the finding map of this statement for current condition
    std::vector<SQL_FINDING>::iterator iter;
    for (iter = finding_vec.begin(); iter != finding_vec.end(); iter++)
      if (iter->code == err_no) break;
    if (iter == finding_vec.cend()) {
      /* If we reached the SQL stats limits then skip adding new findings
         i.e, keep updating the count and date of existing findings
       */
      if (is_sql_stats_collection_above_limit()) continue;

      // First time this finding is reported for this statement
      SQL_FINDING sql_find;
      sql_find.code = err_no;
      sql_find.level = err->severity();
      sql_find.message.append(
          err->message_text(),
          std::min((uint)err->message_octet_length(), sf_max_message_size));
      sql_find.query_text.append(
          query_text, std::min((uint)strlen(query_text), sf_max_query_size));
      sql_find.count = 1;
      sql_find.last_recorded = now;
      finding_vec.push_back(sql_find);

      sql_findings_size += sizeof(SQL_FINDING);
      sql_findings_size += sql_find.message.size();
      sql_findings_size += sql_find.query_text.size();
    } else {
      // Increment the count and update the time
      iter->count++;
      iter->last_recorded = now;
    }
  }
}

bool should_store_findings(enum_sql_command sql_command) {
  switch (sql_command) {
    case SQLCOM_SELECT:
    case SQLCOM_CREATE_TABLE:
    case SQLCOM_CREATE_INDEX:
    case SQLCOM_UPDATE:
    case SQLCOM_UPDATE_MULTI:
    case SQLCOM_ALTER_TABLE:
    case SQLCOM_DELETE:
    case SQLCOM_DELETE_MULTI:
    case SQLCOM_DROP_TABLE:
    case SQLCOM_DROP_INDEX:
    case SQLCOM_INSERT:
    case SQLCOM_INSERT_SELECT:
    case SQLCOM_REPLACE:
    case SQLCOM_REPLACE_SELECT:
    case SQLCOM_LOAD:
    case SQLCOM_TRUNCATE:
      return true;
    default:
      return false;
  }
}

/*
  store_sql_findings
    Store the findings for the SQL statement that just ended into
    the corresponding findings map that is looked up in the global
    map using the SQL ID of the statement. The bulk of the work is
    done in populate_sql_findings()

  Input:
    thd         in:  - THD
    query_text  in:  - text of the SQL statement
*/
void store_sql_findings(THD *thd, char *query_text) {
  if (sql_findings_control == SQL_INFO_CONTROL_ON &&
      thd->mt_key_is_set(THD::SQL_ID) && 
      (thd->get_stmt_da()->cond_count() > 0) &&
      should_store_findings(thd->lex->sql_command)) {
    mysql_mutex_lock(&LOCK_global_sql_findings);

    // Lookup finding map for this statement
    auto sql_find_it =
        global_sql_findings_map.find(thd->mt_key_value(THD::SQL_ID));
    if (sql_find_it == global_sql_findings_map.end()) {
      /* Check whether we reached the SQL stats limits  */
      if (!is_sql_stats_collection_above_limit()) {
        // First time a finding is reported for this statement
        SQL_FINDING_VEC finding_vec;
        populate_sql_findings(thd, query_text, finding_vec);

        global_sql_findings_map.insert(
            std::make_pair(thd->mt_key_value(THD::SQL_ID), finding_vec));

        sql_findings_size += DIGEST_HASH_SIZE;  // for SQL_ID
      }
    } else {
      populate_sql_findings(thd, query_text, sql_find_it->second);
    }

    mysql_mutex_unlock(&LOCK_global_sql_findings);
  }
}

std::vector<sql_findings_row> get_all_sql_findings() {
  std::vector<sql_findings_row> sql_findings;
  mysql_mutex_lock(&LOCK_global_sql_findings);

  for (auto sql_iter = global_sql_findings_map.cbegin();
       sql_iter != global_sql_findings_map.cend(); ++sql_iter) {
    /* Generate the DIGEST string from the digest */
    char sql_id_string[DIGEST_HASH_TO_STRING_LENGTH + 1];
    DIGEST_HASH_TO_STRING(sql_iter->first.data(), sql_id_string);
    sql_id_string[DIGEST_HASH_TO_STRING_LENGTH] = '\0';

    for (auto f_iter = sql_iter->second.cbegin();
         f_iter != sql_iter->second.cend(); ++f_iter) {
      sql_findings.emplace_back(
          sql_id_string,                           // SQL_ID
          f_iter->code,                            // CODE
          warning_level_names[f_iter->level].str,  // LEVEL
          f_iter->message.c_str(),                 // MESSAGE
          f_iter->query_text.c_str(),              // QUERY_TEXT
          f_iter->count,                           // COUNT
          f_iter->last_recorded);                  // LAST_RECORDED
    }
  }

  mysql_mutex_unlock(&LOCK_global_sql_findings);
  return sql_findings;
}

/***********************************************************************
                End - Functions to support SQL findings
************************************************************************/
