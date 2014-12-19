# Copyright (c) 2011, 2012, Oracle and/or its affiliates. All rights reserved.
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
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

MACRO (MYSQL_USE_BUNDLED_ROCKSDB)
  IF (NOT EXISTS "${CMAKE_SOURCE_DIR}/rocksdb/Makefile")
    MESSAGE(SEND_ERROR "Missing Makefile in rocksdb directory. Try \"git submodule update\".")
  ENDIF()
  SET(ROCKSDB_LIBRARY  ${CMAKE_SOURCE_DIR}/rocksdb)
  SET(ROCKSDB_INCLUDE_DIR  ${CMAKE_SOURCE_DIR}/rocksdb/include)
  SET(ROCKSDB_FOUND TRUE)
  SET(WITH_ROCKSDB "bundled" CACHE STRING "Use bundled rocksdb")
  GET_TARGET_PROPERTY(src rocksdb SOURCES)
  FOREACH(file ${src})
    SET(ROCKSDB_SOURCES ${ROCKSDB_SOURCES} ${CMAKE_SOURCE_DIR}/rocksdb/${file})
  ENDFOREACH()
  EXECUTE_PROCESS(
    COMMAND env -u CXXFLAGS make release
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/rocksdb
    RESULT_VARIABLE make_result
  )
ENDMACRO()

# MYSQL_CHECK_ROCKSDB
#
# Provides the following configure options:
# WITH_ROCKSDB
# If this is set to "system", search for system rocksdb
# Otherwise, we use bundled rocksdb
# if system rocksdb is not found, use bundled copy
# ROCKSDB_LIBRARY, ROCKSDB_INCLUDE_DIR and ROCKSDB_SOURCES
# are set after this macro has run

MACRO (MYSQL_CHECK_ROCKSDB)

  IF (NOT WITH_ROCKSDB)
    SET(WITH_ROCKSDB "bundled"  CACHE STRING "By default use bundled rocksdb on this platform")
  ENDIF()

  IF (WITH_ROCKSDB STREQUAL "bundled")
    MYSQL_USE_BUNDLED_ROCKSDB()
  ELSEIF (WITH_ROCKSDB STREQUAL "system" OR WITH_ROCKSDB STREQUAL "yes")
    SET(ROCKSDB_FIND_QUIETLY TRUE)

    IF (NOT ROCKSDB_INCLUDE_PATH)
      set(ROCKSDB_INCLUDE_PATH /usr/local/include /opt/local/include)
    ENDIF()

    find_path(ROCKSDB_INCLUDE_DIR db.h PATHS ${ROCKSDB_INCLUDE_PATH})

    IF (NOT ROCKSDB_INCLUDE_DIR)
        MESSAGE(SEND_ERROR "Cannot find appropriate db.h in /usr/local/include or /opt/local/include. Use bundled rocksdb")
    ENDIF()

    IF (NOT ROCKSDB_LIB_PATHS)
      set(ROCKSDB_LIB_PATHS /usr/local/lib /opt/local/lib)
    ENDIF()

    find_library(ROCKSDB_LIB rocksdb PATHS ${ROCKSDB_LIB_PATHS})

    IF (NOT ROCKSDB_LIB)
        MESSAGE(SEND_ERROR "Cannot find appropriate rocksdb lib in /usr/local/lib or /opt/local/lib. Use bundled rocksdb")
    ENDIF()

    IF (ROCKSDB_LIB AND ROCKSDB_INCLUDE_DIR)
      set(ROCKSDB_FOUND TRUE)
      set(ROCKSDB_LIBS ${ROCKSDB_LIB})
    ELSE()
      set(ROCKSDB_FOUND FALSE)
    ENDIF()

    IF(ROCKSDB_FOUND)
      SET(ROCKSDB_SOURCES "")
      SET(ROCKSDB_LIBRARY ${ROCKSDB_LIBS})
      SET(ROCKSDB_INCLUDE_DIRS ${ROCKSDB_INCLUDE_DIR})
      SET(ROCKSDB_DEFINES "-DHAVE_ROCKSDB")
    ELSE()
      IF(WITH_ROCKSDB STREQUAL "system")
        MESSAGE(SEND_ERROR "Cannot find appropriate system libraries for rocksdb. Use bundled rocksdb")
      ENDIF()
      MYSQL_USE_BUNDLED_ROCKSDB()
    ENDIF()

  ENDIF()
ENDMACRO()
