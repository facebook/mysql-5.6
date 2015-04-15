# TODO: Copyrights
#
# Optional environment variables that may be used, if library
# files are located in non-standard path:
#
# ROCKSDB_INCLUDE=   # path to rocksdb include directory
# ROCKSDB_LIBRARIES= # path to rocksdb shared or static libraries

SET(ROCKSDB_SE_PLUGIN_STATIC  "rocksdb")


FIND_PATH(Rocksdb_INCLUDE_DIRS rocksdb/db.h PATHS
${Rocksdb_INCLUDE_DIRS}
$ENV{ROCKSDB_INCLUDE}      # environment variable to be used optionally
${ROCKSDB_INCLUDE_DIR}     # this may be set
)
IF(NOT EXISTS ${Rocksdb_INCLUDE_DIRS})
  MESSAGE(SEND_ERROR "No rocksdb include directory found! Consider using environment variable ROCKSDB_INCLUDE=...")
  RETURN()
ENDIF()

FIND_LIBRARY(Rocksdb_LIBS NAMES rocksdb
             PATHS ${Rocksdb_LIB} ${ROCKSDB_LIBRARY} $ENV{ROCKSDB_LIBRARIES})
IF(EXISTS ${Rocksdb_LIBS})
  GET_FILENAME_COMPONENT(LINK_DIR ${Rocksdb_LIBS} ABSOLUTE)
ELSE()
  MESSAGE(SEND_ERROR "No rocksdb library directory found! Consider using environment variable ROCKSDB_LIBRARIES=...")
  RETURN()
ENDIF()

INCLUDE_DIRECTORIES(AFTER ${Rocksdb_INCLUDE_DIRS})
SET(CMAKE_REQUIRED_INCLUDES ${Rocksdb_INCLUDE_DIRS})

# SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --std=c++11")

CHECK_CXX_SOURCE_COMPILES(
"
#include <rocksdb/db.h>
int main() { return 0; }
" ROCKSDB_OK)

IF(ROCKSDB_OK)
  SET(ROCKSDB_SOURCES
      ha_rocksdb.cc ha_rocksdb.h ha_rocksdb_proto.h
      rdb_datadic.cc rdb_datadic.h
      rdb_locks.cc rdb_locks.h
      rdb_rowmods.cc rdb_rowmods.h
      rdb_applyiter.cc rdb_applyiter.h
      rdb_cf_options.cc rdb_cf_options.h
      rdb_cf_manager.cc rdb_cf_manager.h
      rdb_dropped_indices.cc rdb_dropped_indices.h
      properties_collector.cc properties_collector.h
      rdb_i_s.cc rdb_i_s.h
      )


  # Link correct versions of libraries

  SET(rocksdb_static_libs )

  IF (NOT "$ENV{WITH_LZ4}" STREQUAL "")
    SET(rocksdb_static_libs ${rocksdb_static_libs} $ENV{WITH_LZ4}/lib/liblz4.a)
  ELSE()
    SET(rocksdb_static_libs ${rocksdb_static_libs} lz4)
  ENDIF()

  IF (NOT "$ENV{WITH_BZ2}" STREQUAL "")
    SET(rocksdb_static_libs ${rocksdb_static_libs} $ENV{WITH_BZ2}/lib/libbz2.a)
  ELSE()
    SET(rocksdb_static_libs ${rocksdb_static_libs} bz2)
  ENDIF()

  IF (NOT "$ENV{WITH_SNAPPY}" STREQUAL "")
    SET(rocksdb_static_libs ${rocksdb_static_libs} $ENV{WITH_SNAPPY}/lib/libsnappy.a)
  ELSE()
    SET(rocksdb_static_libs ${rocksdb_static_libs} snappy)
  ENDIF()


  # Add plugin

  MYSQL_ADD_PLUGIN(rocksdb_se ${ROCKSDB_SOURCES} STORAGE_ENGINE STATIC_ONLY LINK_LIBRARIES ${LINK_DIR} ${rocksdb_static_libs})

  ADD_SUBDIRECTORY(unittest)
ENDIF(ROCKSDB_OK)
