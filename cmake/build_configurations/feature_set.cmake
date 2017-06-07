# Copyright (c) 2012, Oracle and/or its affiliates. All rights reserved.
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

SET(FEATURE_SET "community" CACHE STRING
" Selection of features. Options are
 - xsmall:
 - small:     embedded
 - classic:   embedded + archive + federated + blackhole
 - large:     embedded + archive + federated + blackhole + innobase
 - xlarge:    embedded + archive + federated + blackhole + innobase + partition + rocksdb
 - community: all features (currently == xlarge)
 Alternatively, you can select one or more features providing a semicolon-separated list.
"
)

SET(FEATURE_SET_xsmall    1)
SET(FEATURE_SET_small     2)
SET(FEATURE_SET_classic   3)
SET(FEATURE_SET_large     5)
SET(FEATURE_SET_xlarge    6)
SET(FEATURE_SET_community 7)

SET(FEATURE_LIST EMBEDDED ARCHIVE BLACKHOLE EXAMPLE FEDERATED INNOBASE PARTITION ROCKSDB)

IF(FEATURE_SET)
  STRING(TOLOWER ${FEATURE_SET} feature_set)
  SET(num ${FEATURE_SET_${feature_set}})

  IF(num)
    IF(num EQUAL FEATURE_SET_xsmall)
      SET(WITH_NONE ON)
    ENDIF()

    IF(num GREATER FEATURE_SET_xsmall)
      SET(WITH_EMBEDDED_SERVER ON CACHE BOOL "")
    ENDIF()
    IF(num GREATER FEATURE_SET_small)
      SET(WITH_ARCHIVE_STORAGE_ENGINE  ON)
      SET(WITH_BLACKHOLE_STORAGE_ENGINE ON)
      SET(WITH_FEDERATED_STORAGE_ENGINE ON)
    ENDIF()
    IF(num GREATER FEATURE_SET_classic)
      SET(WITH_INNOBASE_STORAGE_ENGINE ON)
    ENDIF()
    IF(num GREATER FEATURE_SET_large)
      SET(WITH_PARTITION_STORAGE_ENGINE ON)
      SET(WITH_ROCKSDB_STORAGE_ENGINE ON)
    ENDIF()
    IF(num GREATER FEATURE_SET_xlarge)
     # OPTION(WITH_ALL ON)
     # better no set this, otherwise server would be linked
     # statically with experimental stuff like audit_null
    ENDIF()
  ELSE()
    FOREACH(feature ${FEATURE_SET})
      STRING(TOUPPER ${feature} FEATURE)
      LIST(FIND FEATURE_LIST ${FEATURE} feature_index)
      IF(${feature_index} GREATER -1)
        IF("x${FEATURE}" STREQUAL "xEMBEDDED")
          SET(WITH_EMBEDDED_SERVER ON CACHE BOOL "")
        ELSE()
          SET(WITH_${FEATURE}_STORAGE_ENGINE ON)
        ENDIF()
      ELSE()
        MESSAGE(FATAL_ERROR "Unknown feature '${feature}'. Known features\
        include: embedded, archive, blackhole, example, federated, innobase,\
        partition and rocksdb. Alternatively you can use one of the following\
        predefined feature set: xsmall, small, classic, large, xlarge or\
        community.")
      ENDIF()
    ENDFOREACH()
  ENDIF()

  # Update cache with current values, remove engines we do not care about
  # from build.
  FOREACH(FEATURE ${FEATURE_LIST})
    IF("x${FEATURE}" STREQUAL "xEMBEDDED")
      CONTINUE()
    ENDIF()
    IF(NOT WITH_${FEATURE}_STORAGE_ENGINE)
      SET(WITHOUT_${FEATURE}_STORAGE_ENGINE ON CACHE BOOL "")
      MARK_AS_ADVANCED(WITHOUT_${FEATURE}_STORAGE_ENGINE)
      SET(WITH_${FEATURE}_STORAGE_ENGINE OFF CACHE BOOL "")
    ELSE()
     SET(WITH_${FEATURE}_STORAGE_ENGINE ON CACHE BOOL "")
    ENDIF()
  ENDFOREACH()
ENDIF()

SET(WITH_SSL system CACHE STRING "")
SET(WITH_ZLIB bundled CACHE STRING "")
