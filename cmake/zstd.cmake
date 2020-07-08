# Copyright (c) 2019, 2021, Oracle and/or its affiliates.
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

# cmake -DWITH_ZSTD=system|bundled|path_to_zstd
# system is the default

# With earier versions, several compression tests fail.
# With version < 1.0.0 our source code does not build.
SET(MIN_ZSTD_VERSION_REQUIRED "1.2.0")

MACRO (FIND_ZSTD_VERSION)
  FOREACH(version_part
      ZSTD_VERSION_MAJOR
      ZSTD_VERSION_MINOR
      ZSTD_VERSION_RELEASE
      )
    FILE(STRINGS "${ZSTD_INCLUDE_DIR}/zstd.h" ${version_part}
      REGEX "^#[\t ]*define[\t ]+${version_part}[\t ]+([0-9]+).*")
    STRING(REGEX REPLACE
      "^.*${version_part}[\t ]+([0-9]+).*" "\\1"
      ${version_part} "${${version_part}}")
  ENDFOREACH()
  SET(ZSTD_VERSION
    "${ZSTD_VERSION_MAJOR}.${ZSTD_VERSION_MINOR}.${ZSTD_VERSION_RELEASE}")
  SET(ZSTD_VERSION "${ZSTD_VERSION}" CACHE INTERNAL "ZSTD major.minor.step")
  MESSAGE(STATUS "ZSTD_VERSION (${WITH_ZSTD}) is ${ZSTD_VERSION}")
  MESSAGE(STATUS "ZSTD_INCLUDE_DIR ${ZSTD_INCLUDE_DIR}")
ENDMACRO()

MACRO (FIND_SYSTEM_ZSTD)
  FIND_PATH(ZSTD_INCLUDE_DIR
    NAMES zstd.h
    PATH_SUFFIXES include)
  FIND_LIBRARY(ZSTD_SYSTEM_LIBRARY
    NAMES zstd
    PATH_SUFFIXES lib)
  IF (ZSTD_INCLUDE_DIR AND ZSTD_SYSTEM_LIBRARY)
    SET(SYSTEM_ZSTD_FOUND 1)
    SET(ZSTD_LIBRARY ${ZSTD_SYSTEM_LIBRARY})
    IF(NOT ZSTD_INCLUDE_DIR STREQUAL "/usr/include")
      # In case of -DCMAKE_PREFIX_PATH=</path/to/custom/zstd>
      INCLUDE_DIRECTORIES(BEFORE SYSTEM ${ZSTD_INCLUDE_DIR})
    ENDIF()
  ENDIF()
ENDMACRO()

SET(ZSTD_VERSION_DIR "zstd-1.5.0")
SET(BUNDLED_ZSTD_PATH ${CMAKE_SOURCE_DIR}/extra/zstd/${ZSTD_VERSION_DIR}/lib)

MACRO (MYSQL_USE_BUNDLED_ZSTD)
  SET(ZSTD_LIBRARY zstd CACHE INTERNAL "Bundled zlib library")
  SET(WITH_ZSTD "bundled" CACHE STRING "Use bundled zstd")
  SET(ZSTD_INCLUDE_DIR ${BUNDLED_ZSTD_PATH})
  INCLUDE_DIRECTORIES(BEFORE SYSTEM ${ZSTD_INCLUDE_DIR})
  ADD_SUBDIRECTORY(extra/zstd)
ENDMACRO()

MACRO (MYSQL_CHECK_ZSTD)
  IF(NOT WITH_ZSTD)
    SET(WITH_ZSTD "system" CACHE STRING "By default use bundled zstd library")
  ENDIF()

  # See if WITH_ZSTD is of the form </path/to/custom/installation>
  FILE(GLOB WITH_ZSTD_HEADER ${WITH_ZSTD}/include/zstd.h)
  IF (WITH_ZSTD_HEADER)
    SET(WITH_ZSTD_PATH ${WITH_ZSTD}
      CACHE PATH "Path to custom ZSTD installation")
  ENDIF()

  IF (WITH_ZSTD STREQUAL "bundled")
    MYSQL_USE_BUNDLED_ZSTD()
  ELSEIF(WITH_ZSTD STREQUAL "system")
    FIND_SYSTEM_ZSTD()
    IF (NOT SYSTEM_ZSTD_FOUND)
      MESSAGE(FATAL_ERROR "Cannot find system zstd libraries.")
    ENDIF()
  ELSEIF(WITH_ZSTD_PATH)
    # First search in WITH_ZSTD_PATH.
    FIND_PATH(ZSTD_ROOT_DIR
      NAMES include/zstd.h
      NO_CMAKE_PATH
      NO_CMAKE_ENVIRONMENT_PATH
      HINTS ${WITH_ZSTD_PATH}
    )

    # Then search in standard places (if not found above).
    FIND_PATH(ZSTD_ROOT_DIR
      NAMES include/zstd.h
    )

    FIND_PATH(ZSTD_INCLUDE_DIR
      NAMES zstd.h
      HINTS ${ZSTD_ROOT_DIR}/include
    )

    FIND_LIBRARY(ZSTD_LIBRARY
      NAMES zstd_pic zstd
      HINTS ${ZSTD_ROOT_DIR}/lib)
    IF(NOT(ZSTD_INCLUDE_DIR AND ZSTD_LIBRARY))
      MESSAGE(FATAL_ERROR "Cannot find appropriate libraries for zstd for WITH_ZSTD=${WITH_ZSTD}.")
    ENDIF()
  ELSE()
    MESSAGE(FATAL_ERROR "Cannot find appropriate libraries for zstd.")
  ENDIF()
  FIND_ZSTD_VERSION()
  IF(ZSTD_VERSION VERSION_LESS MIN_ZSTD_VERSION_REQUIRED)
    MESSAGE(FATAL_ERROR
      "ZSTD version must be at least ${MIN_ZSTD_VERSION_REQUIRED}, "
      "found ${ZSTD_VERSION}.\nPlease use -DWITH_ZSTD=bundled")
  ENDIF()
ENDMACRO()
