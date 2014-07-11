# TODO: Copyrights
#
# Optional environment variables that may be used, if library
# files are located in non-standard path:
#
# LEVELDB_INCLUDE=   # path to leveldb include directory
# LEVELDB_LIBRARIES= # path to leveldb shared or static libraries

SET(LEVELDB_SE_PLUGIN_STATIC  "leveldb")
SET(LEVELDB_SE_PLUGIN_MANDATORY FALSE)

FIND_PATH(Leveldb_INCLUDE_DIRS leveldb/db.h PATHS
$ENV{LEVELDB_INCLUDE}      # environment variable to be used optionally
${Leveldb_INCLUDE_DIR}     # this may be set
/usr/local/include/leveldb # list of additional directories to look from
/opt/local/include/leveldb
/usr/include/leveldb
/opt/include/leveldb
)
IF(NOT EXISTS ${Leveldb_INCLUDE_DIRS})
  # MESSAGE(SEND_ERROR "No leveldb include directory found! Consider using environment variable LEVELDB_INCLUDE=...")
  RETURN()
ENDIF()

FIND_LIBRARY(Leveldb_LIBS NAMES leveldb
             PATHS ${Leveldb_LIB_PATHS} ${Leveldb_LIB} $ENV{LEVELDB_LIBRARIES})
IF(EXISTS ${Leveldb_LIBS})
  GET_FILENAME_COMPONENT(LINK_DIR ${Leveldb_LIBS} ABSOLUTE)
ELSE()
  # MESSAGE(SEND_ERROR "No leveldb library directory found! Consider using environment variable LEVELDB_LIBRARIES=...")
  RETURN()
ENDIF()

INCLUDE_DIRECTORIES(AFTER ${Leveldb_INCLUDE_DIRS})
SET(CMAKE_REQUIRED_INCLUDES ${Leveldb_INCLUDE_DIRS})

CHECK_CXX_SOURCE_COMPILES(
"
#include <leveldb/db.h>
int main() { return 0; }
" LEVELDB_OK)

IF(LEVELDB_OK)
  SET(LEVELDB_SOURCES
      ha_leveldb.cc ha_leveldb.h
      ldb_datadic.cc ldb_datadic.h
      ldb_locks.cc ldb_locks.h
      ldb_rowmods.cc ldb_rowmods.h
      ldb_applyiter.cc ldb_applyiter.h)

  MYSQL_ADD_PLUGIN(leveldb_se ${LEVELDB_SOURCES} STORAGE_ENGINE STATIC_ONLY MANDATORY LINK_LIBRARIES ${LINK_DIR})

  ADD_SUBDIRECTORY(unittest)
ENDIF(LEVELDB_OK)
