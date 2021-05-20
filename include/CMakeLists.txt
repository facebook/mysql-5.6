# Copyright (c) 2006, 2012, Oracle and/or its affiliates. All rights reserved.
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA

SET(HEADERS_GEN_CONFIGURE 
${CMAKE_CURRENT_BINARY_DIR}/mysql_version.h
${CMAKE_CURRENT_BINARY_DIR}/my_config.h
${CMAKE_CURRENT_BINARY_DIR}/mysqld_ername.h
${CMAKE_CURRENT_BINARY_DIR}/mysqld_error.h
${CMAKE_CURRENT_BINARY_DIR}/sql_state.h
)
SET(HEADERS_ABI 
  mysql.h 
  mysql_com.h
  mysql_time.h 
  my_list.h
  my_alloc.h
  typelib.h 
)

SET(HEADERS 
  ${HEADERS_ABI} 
  my_dbug.h 
  m_string.h
  my_sys.h 
  my_xml.h
  mysql_embed.h
  my_pthread.h
  decimal.h
  errmsg.h
  my_global.h
  my_net.h 
  my_getopt.h
  sslopt-longopts.h
  my_dir.h 
  sslopt-vars.h
  sslopt-case.h
  sql_common.h
  native_procedure.h
  keycache.h
  m_ctype.h
  my_attribute.h 
  my_base.h
  my_compiler.h
  mysql_com_server.h
  my_byteorder.h
  byte_order_generic.h
  byte_order_generic_x86.h
  byte_order_generic_x86_64.h
  little_endian.h
  big_endian.h
  ${HEADERS_GEN_CONFIGURE}
)

OPTION(INSTALL_EXTRA_HEADERS "Install additional headers" OFF)
IF (INSTALL_EXTRA_HEADERS)
  SET(HEADERS
    ${HEADERS}
    atomic_stat.h
    ft_global.h
    hash.h
    lf.h
    my_atomic.h
    my_bitmap.h
    my_compare.h
    my_io_perf.h
    my_md5.h
    my_rdtsc.h
    my_stacktrace.h
    my_time.h
    my_timer.h
    myisampack.h
    thr_lock.h
  )
  SET(HEADERS_MYSQL_DIR
    mysql/plugin.h
    mysql/plugin_audit.h
    mysql/plugin_ftparser.h
    mysql/plugin_validate_password.h
    mysql/plugin_multi_tenancy.h
    mysql/thread_pool_priv.h
  )
  SET(HEADERS_ATOMIC_DIR
    atomic/gcc_builtins.h
    atomic/generic-msvc.h
    atomic/nolock.h
    atomic/rwlock.h
    atomic/solaris.h
  )
  SET(HEADERS_SQL_DIR
    ../sql/datadict.h
    ../sql/debug_sync.h
    ../sql/field.h
    ../sql/filesort_utils.h
    ../sql/handler.h
    ../sql/hh_wheel_timer.h
    ../sql/log.h
    ../sql/mdl.h
    ../sql/my_decimal.h
    ../sql/mysqld.h
    ../sql/parse_file.h
    ../sql/rpl_gtid.h
    ../sql/scheduler.h
    ../sql/sql_alloc.h
    ../sql/sql_array.h
    ../sql/sql_bitmap.h
    ../sql/sql_cache.h
    ../sql/sql_cmd.h
    ../sql/sql_const.h
    ../sql/sql_digest.h
    ../sql/sql_error.h
    ../sql/sql_list.h
    ../sql/sql_plist.h
    ../sql/sql_plugin.h
    ../sql/sql_priv.h
    ../sql/sql_profile.h
    ../sql/sql_show.h
    ../sql/sql_string.h
    ../sql/sql_table.h
    ../sql/structs.h
    ../sql/table.h
    ../sql/table_id.h
    ../sql/thread_iterator.h
    ../sql/thr_malloc.h
    ../sql/timer.h
    ../sql/unireg.h
  )
  SET(HEADERS_FBSON_DIR
    ../fbson/FbsonDocument.h
    ../fbson/FbsonJsonParser.h
    ../fbson/FbsonUpdater.h
    ../fbson/FbsonUtil.h
    ../fbson/FbsonError.h
    ../fbson/FbsonJsonParser.h
    ../fbson/FbsonStream.h
    ../fbson/FbsonWriter.h
  )
ENDIF()

INSTALL(FILES ${HEADERS} DESTINATION ${INSTALL_INCLUDEDIR} COMPONENT Development)
INSTALL(DIRECTORY mysql/ DESTINATION ${INSTALL_INCLUDEDIR}/mysql COMPONENT Development 
 PATTERN "*.h"
 PATTERN "psi_abi*" EXCLUDE
)
INSTALL(FILES ${HEADERS_ATOMIC_DIR}
        DESTINATION ${INSTALL_INCLUDEDIR}/atomic COMPONENT Development)
INSTALL(FILES ${HEADERS_MYSQL_DIR}
        DESTINATION ${INSTALL_INCLUDEDIR}/mysql COMPONENT Development)
INSTALL(FILES ${HEADERS_SQL_DIR}
        DESTINATION ${INSTALL_INCLUDEDIR}/sql COMPONENT Development)
INSTALL(FILES ${HEADERS_FBSON_DIR}
        DESTINATION ${INSTALL_INCLUDEDIR}/fbson COMPONENT Development)
