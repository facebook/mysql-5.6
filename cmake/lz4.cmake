# Copyright (c) 2015, 2021, Oracle and/or its affiliates.
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

# cmake -DWITH_LZ4=system|bundled|3rdparty
# bundled is the default

SET(LIBLZ4_VERSION_REQUIRED "1.8.0")  # LZ4F_HEADER_SIZE_MAX and LZ4F_freeDecompressionContext

MACRO (CHECK_LZ4_VERSION)
  SET(PATH_TO_LZ4_H "${ARGV0}/lz4.h")
  IF (NOT EXISTS ${PATH_TO_LZ4_H})
    MESSAGE(FATAL_ERROR "File ${PATH_TO_LZ4_H} not found")
  ENDIF()

  FILE(STRINGS "${PATH_TO_LZ4_H}" LIBLZ4_HEADER_CONTENT REGEX "#define LZ4_VERSION_[A-Z]+ +[0-9]+")
  STRING(REGEX REPLACE ".*#define LZ4_VERSION_MAJOR +([0-9]+).*" "\\1" LIBLZ4_VERSION_MAJOR "${LIBLZ4_HEADER_CONTENT}")
  STRING(REGEX REPLACE ".*#define LZ4_VERSION_MINOR +([0-9]+).*" "\\1" LIBLZ4_VERSION_MINOR "${LIBLZ4_HEADER_CONTENT}")
  STRING(REGEX REPLACE ".*#define LZ4_VERSION_RELEASE +([0-9]+).*" "\\1" LIBLZ4_VERSION_RELEASE "${LIBLZ4_HEADER_CONTENT}")
  SET(LIBLZ4_VERSION_STRING "${LIBLZ4_VERSION_MAJOR}.${LIBLZ4_VERSION_MINOR}.${LIBLZ4_VERSION_RELEASE}")
  UNSET(LIBLZ4_HEADER_CONTENT)

  IF (${LIBLZ4_VERSION_STRING} VERSION_LESS ${LIBLZ4_VERSION_REQUIRED})
    MESSAGE(FATAL_ERROR "Required liblz4 ${LIBLZ4_VERSION_REQUIRED} and installed version is ${LIBLZ4_VERSION_STRING}")
  ELSE()
    MESSAGE(STATUS "Found liblz4 version ${LIBLZ4_VERSION_STRING}")
  ENDIF()
ENDMACRO()

MACRO (FIND_SYSTEM_LZ4)
  FIND_PATH(LZ4_INCLUDE_DIR
    NAMES lz4frame.h)
  FIND_LIBRARY(LZ4_SYSTEM_LIBRARY
    NAMES lz4)
  IF (LZ4_INCLUDE_DIR AND LZ4_SYSTEM_LIBRARY)
    CHECK_LZ4_VERSION(${PATH_TO_LZ4})
    SET(SYSTEM_LZ4_FOUND 1)
    SET(LZ4_LIBRARY ${LZ4_SYSTEM_LIBRARY})
  ENDIF()
ENDMACRO()

SET(LZ4_VERSION "lz4-1.9.3")
SET(BUNDLED_LZ4_PATH "${CMAKE_SOURCE_DIR}/extra/lz4/${LZ4_VERSION}")

MACRO (MYSQL_USE_BUNDLED_LZ4)
  SET(WITH_LZ4 "bundled" CACHE STRING "Bundled lz4 library")
  SET(BUILD_BUNDLED_LZ4 1)
  CHECK_LZ4_VERSION(${BUNDLED_LZ4_PATH})
  INCLUDE_DIRECTORIES(BEFORE SYSTEM ${BUNDLED_LZ4_PATH})
  SET(LZ4_LIBRARY lz4_lib)
  ADD_LIBRARY(lz4_lib STATIC
    ${BUNDLED_LZ4_PATH}/lz4.c
    ${BUNDLED_LZ4_PATH}/lz4frame.c
    ${BUNDLED_LZ4_PATH}/lz4hc.c
    ${BUNDLED_LZ4_PATH}/xxhash.c
    )
ENDMACRO()

MACRO (MYSQL_CHECK_LZ4)
  IF(NOT WITH_LZ4)
    SET(WITH_LZ4 "bundled" CACHE STRING "By default use bundled lz4 library")
  ENDIF()

  IF(WITH_LZ4 STREQUAL "bundled")
    MYSQL_USE_BUNDLED_LZ4()
  ELSEIF(WITH_LZ4 STREQUAL "system")
    FIND_SYSTEM_LZ4()
    IF (NOT SYSTEM_LZ4_FOUND)
      MESSAGE(FATAL_ERROR "Cannot find system lz4 libraries.") 
    ENDIF()
  ELSEIF(WITH_LZ4 STREQUAL "3rdparty")
    # Support using 3rd party version of lz4 pointed by LZ4_PATH
    CHECK_LZ4_VERSION(${LZ4_PATH}/include)
    INCLUDE_DIRECTORIES(SYSTEM ${LZ4_PATH}/include)
    SET(LZ4_LIBRARY ${LZ4_PATH}/lib/liblz4_pic.a)
  ELSE()
    MESSAGE(FATAL_ERROR "WITH_LZ4 must be bundled, system, or 3rdparty")
  ENDIF()
ENDMACRO()
