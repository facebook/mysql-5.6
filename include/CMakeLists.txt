# Copyright (c) 2006, 2022, Oracle and/or its affiliates.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License, version 2.0,
# as published by the Free Software Foundation.
#
# This program is also distributed with certain software (including
# but not limited to OpenSSL) that is licensed under separate terms,
# as designated in a particular file or component or in included license
# documentation.  The authors of MySQL hereby grant you an additional
# permission to link the program and your derivative works with the
# separately licensed software that they have included with MySQL.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License, version 2.0, for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA

DISABLE_MISSING_PROFILE_WARNING()

# These are the headers required to #include <mysql.h>, which we ship
# to the client as part of our API.
#
# FIXME: mysqld_error.h, which is also part of our API, should be included
# from mysql.h, but this needs more build system tweaking, since it's
# automatically generated.
SET(HEADERS
  errmsg.h
  my_checksum.h
  my_command.h
  my_compress.h
  my_sqlcommand.h
  my_list.h
  mysql.h
  mysql_async.h
  mysql_com.h
  mysql_time.h
  ${CMAKE_CURRENT_BINARY_DIR}/mysql_version.h
  ${CMAKE_CURRENT_BINARY_DIR}/mysqld_error.h
  ${CMAKE_CURRENT_BINARY_DIR}/my_config.h
  m_string.h
  my_sys.h
  field_types.h
  lex_string.h
  my_inttypes.h
  my_macros.h
  my_compiler.h
  my_loglevel.h
  my_sharedlib.h
  my_psi_config.h
  thr_mutex.h
  my_dbug.h
  my_thread.h
  thr_cond.h
  my_io.h
  my_byteorder.h
  template_utils.h
  typelib.h
)
SET(HEADERS_MYSQL_DIR
  mysql/client_plugin.h
  mysql/mysql_lex_string.h
  mysql/plugin_auth_common.h
  mysql/plugin.h
  mysql/plugin_audit.h
  mysql/plugin_audit_message_types.h
  mysql/status_var.h
  mysql/udf_registration_types.h
  mysql/service_mysql_alloc.h
)
SET(HEADERS_MYSQL_PSI_DIR
  mysql/psi/mysql_mutex.h
  mysql/psi/psi_mutex.h
  mysql/psi/mysql_cond.h
  mysql/psi/psi_cond.h
)
SET(HEADERS_SQL_DIR
  ../sql/stream_cipher.h
)

OPTION(INSTALL_EXTRA_HEADERS "Install additional headers" OFF)
IF (INSTALL_EXTRA_HEADERS)
  SET(HEADERS
    ${HEADERS}
    decimal.h
    dur_prop.h
    ft_global.h
    integer_digits.h
    map_helpers.h
    m_ctype.h
    memory_debugging.h
    mem_root_deque.h
    my_alloc.h
    my_base.h
    my_bitmap.h
    my_dir.h
    my_double2ulonglong.h
    my_hostname.h
    my_getopt.h
    my_pointer_arithmetic.h
    my_systime.h
    my_table_map.h
    my_thread_local.h
    my_time.h
    my_time_t.h
    pfs_cond_provider.h
    pfs_mutex_provider.h
    pfs_rwlock_provider.h
    pfs_socket_provider.h
    pfs_stage_provider.h
    pfs_thread_provider.h
    prealloced_array.h
    sql_string.h
    thr_lock.h
    thr_rwlock.h
    violite.h
  )
  SET(HEADERS_LIBBINLOGEVENTS_EXPORT_DIR
    ../libbinlogevents/export/binary_log_funcs.h
  )
  SET(HEADERS_LIBBINLOGEVENTS_INCLUDE_DIR
    ${CMAKE_CURRENT_BINARY_DIR}/binlog_config.h
    ../libbinlogevents/include/table_id.h
    ../libbinlogevents/include/uuid.h
    ../libbinlogevents/include/wrapper_functions.h
  )
  SET(HEADERS_LIBBINLOGEVENTS_INCLUDE_COMPRESSION_DIR
    ../libbinlogevents/include/compression/base.h
  )
  SET(HEADERS_LIBBINLOGEVENTS_INCLUDE_GTIDS_DIR
    ../libbinlogevents/include/gtids/global.h
  )
  SET(HEADERS_MYSQL_DIR
    ${HEADERS_MYSQL_DIR}
    mysql/com_data.h
    mysql/raft_listener_queue_if.h
    mysql/raft_optype.h
    mysql/raft_replication.h
    mysql/service_command.h
    mysql/service_cpu_scheduler.h
    mysql/service_locking.h
    mysql/service_my_plugin_log.h
    mysql/service_mysql_keyring.h
    mysql/service_mysql_password_policy.h
    mysql/service_mysql_string.h
    mysql/service_parser.h
    mysql/service_plugin_registry.h
    mysql/service_privacy.h
    mysql/service_rpc_plugin.h
    mysql/service_rpl_transaction_ctx.h
    mysql/service_rpl_transaction_write_set.h
    mysql/service_rules_table.h
    mysql/service_security_context.h
    mysql/service_srv_session.h
    mysql/service_srv_session_bits.h
    mysql/service_srv_session_info.h
    mysql/service_ssl_wrapper.h
    mysql/service_thd_alloc.h
    mysql/service_thd_engine_lock.h
    mysql/service_thd_wait.h
    mysql/service_thread_scheduler.h
    mysql/services.h
    mysql/thread_pool_priv.h
  )
  SET(HEADERS_MYSQL_COMPONENTS_DIR
    mysql/components/component_implementation.h
    mysql/components/service.h
    mysql/components/service_implementation.h
  )
  SET(HEADERS_MYSQL_COMPONENTS_SERVICES_DIR
    mysql/components/services/dynamic_loader.h
    mysql/components/services/mysql_current_thread_reader.h
    mysql/components/services/mysql_string.h
    mysql/components/services/page_track_service.h
    mysql/components/services/registry.h
    mysql/components/services/system_variable_source_type.h
    mysql/components/services/table_access_service.h
  )
  SET(HEADERS_MYSQL_COMPONENTS_SERVICES_BITS_DIR
    mysql/components/services/bits/my_io_bits.h
    mysql/components/services/bits/mysql_cond_bits.h
    mysql/components/services/bits/mysql_mutex_bits.h
    mysql/components/services/bits/mysql_rwlock_bits.h
    mysql/components/services/bits/mysql_socket_bits.h
    mysql/components/services/bits/my_thread_bits.h
    mysql/components/services/bits/plugin_audit_connection_types.h
    mysql/components/services/bits/psi_bits.h
    mysql/components/services/bits/psi_cond_bits.h
    mysql/components/services/bits/psi_file_bits.h
    mysql/components/services/bits/psi_mdl_bits.h
    mysql/components/services/bits/psi_memory_bits.h
    mysql/components/services/bits/psi_mutex_bits.h
    mysql/components/services/bits/psi_rwlock_bits.h
    mysql/components/services/bits/psi_socket_bits.h
    mysql/components/services/bits/psi_stage_bits.h
    mysql/components/services/bits/psi_statement_bits.h
    mysql/components/services/bits/psi_table_bits.h
    mysql/components/services/bits/psi_thread_bits.h
    mysql/components/services/bits/system_variables_bits.h
    mysql/components/services/bits/table_access_bits.h
    mysql/components/services/bits/thr_cond_bits.h
    mysql/components/services/bits/thr_mutex_bits.h
    mysql/components/services/bits/thr_rwlock_bits.h
  )
  SET(HEADERS_MYSQL_PSI_DIR
    ${HEADERS_MYSQL_PSI_DIR}
    mysql/psi/mysql_rwlock.h
    mysql/psi/mysql_socket.h
    mysql/psi/mysql_stage.h
    mysql/psi/mysql_thread.h
    mysql/psi/psi_memory.h
    mysql/psi/psi_rwlock.h
    mysql/psi/psi_socket.h
    mysql/psi/psi_stage.h
    mysql/psi/psi_thread.h
  )
  SET(HEADERS_SQL_DIR
    ${HEADERS_SQL_DIR}
    ../sql/check_stack.h
    ../sql/debug_sync.h
    ../sql/discrete_interval.h
    ../sql/enum_query_type.h
    ../sql/fb_vector_base.h
    ../sql/field.h
    ../sql/field_common_properties.h
    ../sql/filesort_utils.h
    ../sql/handler.h
    ../sql/item.h
    ../sql/item_cmpfunc.h
    ../sql/item_func.h
    ../sql/item_row.h
    ../sql/key.h
    ../sql/key_spec.h
    ../sql/malloc_allocator.h
    ../sql/mdl.h
    ../sql/mem_root_allocator.h
    ../sql/mem_root_array.h
    ../sql/my_decimal.h
    ../sql/opt_costconstants.h
    ../sql/opt_costmodel.h
    ../sql/parse_location.h
    ../sql/parse_tree_node_base.h
    ../sql/psi_memory_key.h
    ../sql/query_arena.h
    ../sql/query_result.h
    ../sql/record_buffer.h
    ../sql/rpl_gtid.h
    ../sql/rpl_reporting.h
    ../sql/select_lex_visitor.h
    ../sql/snapshot.h
    ../sql/set_var.h
    ../sql/sql_array.h
    ../sql/sql_bitmap.h
    ../sql/sql_cmd.h
    ../sql/sql_cmd_dml.h
    ../sql/sql_const.h
    ../sql/sql_db.h
    ../sql/sql_error.h
    ../sql/sql_list.h
    ../sql/sql_opt_exec_shared.h
    ../sql/sql_plist.h
    ../sql/sql_plugin.h
    ../sql/sql_plugin_ref.h
    ../sql/sql_prepare.h
    ../sql/sql_profile.h
    ../sql/sql_select.h
    ../sql/sql_show.h
    ../sql/sql_sort.h
    ../sql/sql_thd_internal_api.h
    ../sql/sql_udf.h
    ../sql/stateless_allocator.h
    ../sql/table.h
    ../sql/table_trigger_field_support.h
    ../sql/thr_malloc.h
    ../sql/trigger_def.h
  )
  SET(HEADERS_SQL_COMMON_DIR
    ../sql-common/json_binary.h
    ../sql-common/json_dom.h
    ../sql-common/json_error_handler.h
  )
  SET(HEADERS_SQL_CONN_HANDLER_DIR
    ../sql/conn_handler/channel_info.h
    ../sql/conn_handler/connection_handler.h
    ../sql/conn_handler/connection_handler_manager.h
  )
  SET(HEADERS_SQL_DD_DIR
    ../sql/dd/collection.h
    ../sql/dd/object_id.h
    ../sql/dd/properties.h
    ../sql/dd/sdi_fwd.h
    ../sql/dd/string_type.h
  )
  SET(HEADERS_SQL_DD_TYPES_DIR
    ../sql/dd/types/column.h
    ../sql/dd/types/entity_object.h
    ../sql/dd/types/foreign_key.h
    ../sql/dd/types/object_table.h
    ../sql/dd/types/weak_object.h
  )
  SET(HEADERS_SQL_GIS_DIR
    ../sql/gis/srid.h
  )
ENDIF()
SET(HEADERS_INSTALL_LIST
  ${HEADERS}
  big_endian.h
  little_endian.h
)

INSTALL(FILES ${HEADERS_INSTALL_LIST} DESTINATION ${INSTALL_INCLUDEDIR} COMPONENT Development)
INSTALL(FILES ${HEADERS_LIBBINLOGEVENTS_EXPORT_DIR} DESTINATION ${INSTALL_INCLUDEDIR}/libbinlogevents/export COMPONENT Development)
INSTALL(FILES ${HEADERS_LIBBINLOGEVENTS_INCLUDE_DIR} DESTINATION ${INSTALL_INCLUDEDIR}/libbinlogevents/include COMPONENT Development)
INSTALL(FILES ${HEADERS_LIBBINLOGEVENTS_INCLUDE_COMPRESSION_DIR} DESTINATION ${INSTALL_INCLUDEDIR}/libbinlogevents/include/compression COMPONENT Development)
INSTALL(FILES ${HEADERS_LIBBINLOGEVENTS_INCLUDE_GTIDS_DIR} DESTINATION ${INSTALL_INCLUDEDIR}/libbinlogevents/include/gtids COMPONENT Development)
INSTALL(FILES ${HEADERS_MYSQL_DIR} DESTINATION ${INSTALL_INCLUDEDIR}/mysql COMPONENT Development)
INSTALL(FILES ${HEADERS_MYSQL_COMPONENTS_DIR} DESTINATION ${INSTALL_INCLUDEDIR}/mysql/components COMPONENT Development)
INSTALL(FILES ${HEADERS_MYSQL_COMPONENTS_SERVICES_DIR} DESTINATION ${INSTALL_INCLUDEDIR}/mysql/components/services COMPONENT Development)
INSTALL(FILES ${HEADERS_MYSQL_COMPONENTS_SERVICES_BITS_DIR} DESTINATION ${INSTALL_INCLUDEDIR}/mysql/components/services/bits COMPONENT Development)
INSTALL(FILES ${HEADERS_MYSQL_PSI_DIR} DESTINATION ${INSTALL_INCLUDEDIR}/mysql/psi COMPONENT Development)
INSTALL(FILES ${HEADERS_SQL_DIR} DESTINATION ${INSTALL_INCLUDEDIR}/sql COMPONENT Development)
INSTALL(FILES ${HEADERS_SQL_COMMON_DIR} DESTINATION ${INSTALL_INCLUDEDIR}/sql-common COMPONENT Development)
INSTALL(FILES ${HEADERS_SQL_CONN_HANDLER_DIR} DESTINATION ${INSTALL_INCLUDEDIR}/sql/conn_handler COMPONENT Development)
INSTALL(FILES ${HEADERS_SQL_DD_DIR} DESTINATION ${INSTALL_INCLUDEDIR}/sql/dd COMPONENT Development)
INSTALL(FILES ${HEADERS_SQL_DD_TYPES_DIR} DESTINATION ${INSTALL_INCLUDEDIR}/sql/dd/types COMPONENT Development)
INSTALL(FILES ${HEADERS_SQL_GIS_DIR} DESTINATION ${INSTALL_INCLUDEDIR}/sql/gis COMPONENT Development)


# openssl/applink.c must be built into applications linking with libmysql.dll
IF (WIN32 AND OPENSSL_APPLINK_C)
  INSTALL(FILES ${OPENSSL_APPLINK_C}
    DESTINATION ${INSTALL_INCLUDEDIR}/openssl
    COMPONENT Development
    )
ENDIF()

SET(INSTALLED_HEADERS_CC ${CMAKE_CURRENT_BINARY_DIR}/installed_headers.cc)
SET(INSTALLED_HEADERS_CONTENT "")
FOREACH(HEADER ${HEADERS} ${HEADERS_MYSQL_DIR})
  STRING_APPEND(INSTALLED_HEADERS_CONTENT "#include \"${HEADER}\"")
  STRING_APPEND(INSTALLED_HEADERS_CONTENT "\n")
ENDFOREACH()
STRING_APPEND(INSTALLED_HEADERS_CONTENT
  "int main(int, char **) { return 0; }"
  )

CONFIGURE_FILE_CONTENT("${INSTALLED_HEADERS_CONTENT}"
  ${INSTALLED_HEADERS_CC}
  )

# Verifies that all installed headers comply with -std=c++17
MYSQL_ADD_EXECUTABLE(installed_headers
  ${INSTALLED_HEADERS_CC}
  DEPENDENCIES GenError
  SKIP_INSTALL
  )
# This will add -std=gnu++17 for compilers which support it.
# It will be silently ignored on Windows.
SET_PROPERTY(TARGET installed_headers PROPERTY CXX_STANDARD 17)
