# Copyright (c) 2015, 2018, Oracle and/or its affiliates. All rights reserved.
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

MACRO (FIND_SYSTEM_LZ4)
  FIND_PATH(PATH_TO_LZ4 NAMES lz4frame.h)
  FIND_LIBRARY(LZ4_SYSTEM_LIBRARY NAMES lz4)
  IF (PATH_TO_LZ4 AND LZ4_SYSTEM_LIBRARY)
    SET(SYSTEM_LZ4_FOUND 1)
    INCLUDE_DIRECTORIES(SYSTEM ${PATH_TO_LZ4})
    SET(LZ4_LIBRARY ${LZ4_SYSTEM_LIBRARY})
    MESSAGE(STATUS "PATH_TO_LZ4 ${PATH_TO_LZ4}")
    MESSAGE(STATUS "LZ4_LIBRARY ${LZ4_LIBRARY}")
  ENDIF()
ENDMACRO()

IF (NOT WITH_LZ4)
  SET(WITH_LZ4 "system" CACHE STRING "By default use system lz4 library")
ENDIF()

MACRO (MYSQL_CHECK_LZ4)
  # See if WITH_LZ4 is of the form </path/to/custom/installation>
  FILE(GLOB WITH_LZ4_HEADER ${WITH_LZ4}/include/lz4frame.h)
  IF (WITH_LZ4_HEADER)
    SET(WITH_LZ4_PATH ${WITH_LZ4}
      CACHE PATH "Path to custom LZ4 installation")
  ENDIF()

  IF(WITH_LZ4 STREQUAL "system")
    FIND_SYSTEM_LZ4()
    IF (NOT SYSTEM_LZ4_FOUND)
      MESSAGE(FATAL_ERROR "Cannot find system lz4 libraries.")
    ENDIF()
  ELSEIF(WITH_LZ4_PATH)
    # First search in WITH_LZ4_PATH.
    FIND_PATH(LZ4_ROOT_DIR
      NAMES include/lz4frame.h
      NO_CMAKE_PATH
      NO_CMAKE_ENVIRONMENT_PATH
      HINTS ${WITH_LZ4_PATH}
    )

    # Then search in standard places (if not found above).
    FIND_PATH(LZ4_ROOT_DIR
      NAMES include/lz4frame.h
    )

    FIND_PATH(LZ4_INCLUDE_DIR
      NAMES lz4frame.h
      HINTS ${LZ4_ROOT_DIR}/include
    )

    FIND_LIBRARY(LZ4_LIBRARY
      NAMES lz4_pic lz4
      HINTS ${LZ4_ROOT_DIR}/lib)

    IF(LZ4_INCLUDE_DIR AND LZ4_LIBRARY)
      MESSAGE(STATUS "LZ4_INCLUDE_DIR = ${LZ4_INCLUDE_DIR}")
      MESSAGE(STATUS "LZ4_LIBRARY = ${LZ4_LIBRARY}")
    ENDIF()
  ELSE()
    MESSAGE(FATAL_ERROR "WITH_LZ4 must be system or path to library")
  ENDIF()
ENDMACRO()
