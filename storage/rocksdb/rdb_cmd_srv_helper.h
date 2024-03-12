/*
   Copyright (c) 2024, Facebook, Inc.

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

#include <mysql.h>
#include <mysql/components/services/mysql_command_services.h>
#include <cassert>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>
#include "sql-common/json_dom.h"

namespace myrocks {

class Rdb_vector_index_data {
 public:
  Rdb_vector_index_data() {}
  std::vector<float> m_quantizer_codes;
};

class Rdb_cmd_srv_status {
 public:
  Rdb_cmd_srv_status() : m_error(false) {}
  Rdb_cmd_srv_status(const std::string &error_msg)
      : m_error(true), m_error_msg(error_msg) {}
  Rdb_cmd_srv_status(unsigned int error_no, const std::string &error_msg,
                     const std::string &sqlstate_errmsg)
      : m_error(true),
        m_error_no(error_no),
        m_error_msg(error_msg),
        m_sqlstate_errmsg(sqlstate_errmsg) {}

  bool error() const { return m_error; }

  std::string message() const {
    if (m_error) {
      return "error no: " + std::to_string(m_error_no) +
             ", error msg: " + m_error_msg +
             ", sql state: " + m_sqlstate_errmsg;
    }
    return "";
  }

 private:
  bool m_error;
  unsigned int m_error_no;
  std::string m_error_msg;
  std::string m_sqlstate_errmsg;
};

/** helper class to access mysql_command_services */
class Rdb_cmd_srv_helper {
 public:
  Rdb_cmd_srv_helper(
      const mysql_service_mysql_command_factory_t *command_factory,
      const mysql_service_mysql_command_options_t *command_options,
      const mysql_service_mysql_command_query_t *command_query,
      const mysql_service_mysql_command_query_result_t *command_query_result,
      const mysql_service_mysql_command_field_info_t *command_field_info,
      const mysql_service_mysql_command_error_info_t *command_error_info)
      : m_command_factory(command_factory),
        m_command_options(command_options),
        m_command_query(command_query),
        m_command_query_result(command_query_result),
        m_command_field_info(command_field_info),
        m_command_error_info(command_error_info) {}

  /**
   the index data is stored in a table with the following schema:
   create table VECTORDB_DATA (
    id varchar(64) not null,
    type varchar(16) not null,
    seqno int not null,
    value JSON not null,
    primary key (id, type, seqno)
   );

  1. metadata. The type is set to "metadata". The seqno is set to 0.
     Only 1 metadata record is permitted per index.
     The value is a json object with the following fields:
      - version. The version of the index data. Currently set to 1.
  2. type. The type is set to "quantizer". The value is a json array
     of float values. There could be multiple quantizer records per index.
     The records are ordered by seqno when they are read.
  */
  Rdb_cmd_srv_status load_index_data(
      const std::string &db_name, const std::string &table_name,
      const std::string &id,
      std::unique_ptr<Rdb_vector_index_data> &index_data);

 private:
  const mysql_service_mysql_command_factory_t *m_command_factory;
  const mysql_service_mysql_command_options_t *m_command_options;
  const mysql_service_mysql_command_query_t *m_command_query;
  const mysql_service_mysql_command_query_result_t *m_command_query_result;
  const mysql_service_mysql_command_field_info_t *m_command_field_info;
  const mysql_service_mysql_command_error_info_t *m_command_error_info;

  class MYSQL_H_wrapper {
   public:
    MYSQL_H mysql = nullptr;
    explicit MYSQL_H_wrapper(
        const mysql_service_mysql_command_factory_t *command_factory)
        : m_command_factory(command_factory) {
      assert(command_factory);
    }

    mysql_service_status_t init() { return m_command_factory->init(&mysql); }

    ~MYSQL_H_wrapper() {
      if (mysql) {
        m_command_factory->close(mysql);
      }
    }

   private:
    const mysql_service_mysql_command_factory_t *m_command_factory;
  };

  class MYSQL_RES_H_wrapper {
   public:
    MYSQL_RES_H mysql_res = nullptr;
    explicit MYSQL_RES_H_wrapper(
        const mysql_service_mysql_command_query_result_t *command_query_result)
        : m_command_query_result(command_query_result) {
      assert(command_query_result);
    }

    mysql_service_status_t store_result(MYSQL_H mysql_h) {
      return m_command_query_result->store_result(mysql_h, &mysql_res);
    }
    ~MYSQL_RES_H_wrapper() {
      if (mysql_res) {
        m_command_query_result->free_result(mysql_res);
      }
    }

   private:
    const mysql_service_mysql_command_query_result_t *m_command_query_result;
  };

  Rdb_cmd_srv_status connect(MYSQL_H_wrapper &mysql_wrapper);

  Rdb_cmd_srv_status execute_query(const std::string &db_name,
                                   const std::string &query,
                                   MYSQL_H_wrapper &mysql_wrapper,
                                   MYSQL_RES_H_wrapper &mysql_res_wrapper);

  Rdb_cmd_srv_status get_error_status(MYSQL_H_wrapper &mysql_wrapper,
                                      bool connection_error);

  Rdb_cmd_srv_status get_row_count(MYSQL_H_wrapper &mysql_wrapper,
                                   uint64_t &row_count);
  Rdb_cmd_srv_status check_row_count(MYSQL_H_wrapper &mysql_wrapper,
                                     const uint64_t expected_row_count);

  Rdb_cmd_srv_status check_column_types(
      MYSQL_H_wrapper &mysql_wrapper, MYSQL_RES_H_wrapper &mysql_res_wrapper,
      const std::vector<enum_field_types> &field_types);

  Rdb_cmd_srv_status fetch_row(MYSQL_H_wrapper &mysql_wrapper,
                               MYSQL_RES_H_wrapper &mysql_res_wrapper,
                               MYSQL_ROW_H *row, ulong **column_lengths);
  Rdb_cmd_srv_status get_json_column(MYSQL_ROW_H row, ulong *column_lengths,
                                     int col, Json_dom_ptr &dom_ptr,
                                     enum_json_type expected_type);

  Rdb_cmd_srv_status read_index_metadata(const std::string &db_name,
                                         const std::string &table_name,
                                         const std::string &id);
  Rdb_cmd_srv_status read_index_quantizer(const std::string &db_name,
                                          const std::string &table_name,
                                          const std::string &id,
                                          std::vector<float> &codes);
};
}  // namespace myrocks
