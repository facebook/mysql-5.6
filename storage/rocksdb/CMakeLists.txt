# TODO: Copyrights
#
# Optional environment variables that may be used, if library
# files are located in non-standard path:
#
# ROCKSDB_INCLUDE=   # path to rocksdb include directory
# ROCKSDB_LIBRARIES= # path to rocksdb shared or static libraries

SET(ROCKSDB_SE_PLUGIN_STATIC  "rocksdb")
SET(ROCKSDB_SE_PLUGIN_MANDATORY TRUE)


FIND_PATH(Rocksdb_INCLUDE_DIRS rocksdb/db.h PATHS
$ENV{ROCKSDB_INCLUDE}      # environment variable to be used optionally
${Rocksdb_INCLUDE_DIR}     # this may be set
)
IF(NOT EXISTS ${Rocksdb_INCLUDE_DIRS})
  MESSAGE(SEND_ERROR "No rocksdb include directory found! Consider using environment variable ROCKSDB_INCLUDE=...")
  RETURN()
ENDIF()

FIND_LIBRARY(Rocksdb_LIBS NAMES rocksdb
             PATHS ${Rocksdb_LIB_PATHS} ${Rocksdb_LIB} $ENV{ROCKSDB_LIBRARIES})
IF(EXISTS ${Rocksdb_LIBS})
  GET_FILENAME_COMPONENT(LINK_DIR ${Rocksdb_LIBS} ABSOLUTE)
ELSE()
  MESSAGE(SEND_ERROR "No rocksdb library directory found! Consider using environment variable ROCKSDB_LIBRARIES=...")
  RETURN()
ENDIF()

INCLUDE_DIRECTORIES(AFTER ${Rocksdb_INCLUDE_DIRS})
SET(CMAKE_REQUIRED_INCLUDES ${Rocksdb_INCLUDE_DIRS})

# SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --std=c++11")

# TODO is there a better way to do this?
#LINK_LIBRARIES(snappy bz2)

CHECK_CXX_SOURCE_COMPILES(
"
#include <rocksdb/db.h>
int main() { return 0; }
" ROCKSDB_OK)

IF(ROCKSDB_OK)
  SET(ROCKSDB_SOURCES
      ha_rocksdb.cc ha_rocksdb.h
      rdb_datadic.cc rdb_datadic.h
      rdb_locks.cc rdb_locks.h
      rdb_rowmods.cc rdb_rowmods.h
      rdb_applyiter.cc rdb_applyiter.h
      rdb_cf_option.cc rdb_cf_option.h
      rdb_cf_manager.cc rdb_cf_manager.h)

  MYSQL_ADD_PLUGIN(rocksdb_se ${ROCKSDB_SOURCES} STORAGE_ENGINE STATIC_ONLY MANDATORY LINK_LIBRARIES ${LINK_DIR} snappy bz2)

  ADD_SUBDIRECTORY(unittest)
ENDIF(ROCKSDB_OK)
