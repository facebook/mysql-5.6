# TODO: Copyrights

IF (NOT EXISTS "${CMAKE_SOURCE_DIR}/rocksdb/Makefile")
  MESSAGE(SEND_ERROR "Missing Makefile in rocksdb directory. Try \"git submodule update\".")
ENDIF()

# get a list of rocksdb library source files
# run with env -i to avoid passing variables
EXECUTE_PROCESS(
  COMMAND env -i ${CMAKE_SOURCE_DIR}/storage/rocksdb/get_rocksdb_files.sh
  OUTPUT_VARIABLE SCRIPT_OUTPUT
)
# split the list into lines
STRING(REGEX MATCHALL "[^\n]+" ROCKSDB_LIB_SOURCES ${SCRIPT_OUTPUT})

INCLUDE_DIRECTORIES(
  ${CMAKE_SOURCE_DIR}/rocksdb
  ${CMAKE_SOURCE_DIR}/rocksdb/include
  ${CMAKE_SOURCE_DIR}/rocksdb/third-party/gtest-1.7.0/fused-src
)

ADD_DEFINITIONS(-DROCKSDB_PLATFORM_POSIX -DOS_LINUX -DZLIB)

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
  ${ROCKSDB_LIB_SOURCES}
)

SET(rocksdb_static_libs )
IF (NOT "$ENV{WITH_SNAPPY}" STREQUAL "")
  SET(rocksdb_static_libs ${rocksdb_static_libs} $ENV{WITH_SNAPPY}/lib/libsnappy.a)
  ADD_DEFINITIONS(-DSNAPPY)
ELSE()
  SET(rocksdb_static_libs ${rocksdb_static_libs} snappy)
ENDIF()

IF (NOT "$ENV{WITH_LZ4}" STREQUAL "")
  SET(rocksdb_static_libs ${rocksdb_static_libs} $ENV{WITH_LZ4}/lib/liblz4.a)
  ADD_DEFINITIONS(-DLZ4)
ELSE()
  SET(rocksdb_static_libs ${rocksdb_static_libs} lz4)
ENDIF()

IF (NOT "$ENV{WITH_BZ2}" STREQUAL "")
  SET(rocksdb_static_libs ${rocksdb_static_libs} $ENV{WITH_BZ2}/lib/libbz2.a)
  ADD_DEFINITIONS(-DBZIP2)
ELSE()
  SET(rocksdb_static_libs ${rocksdb_static_libs} bz2)
ENDIF()

SET(rocksdb_static_libs ${rocksdb_static_libs} ${ZLIB_LIBRARY})

MYSQL_ADD_PLUGIN(rocksdb_se ${ROCKSDB_SOURCES} STORAGE_ENGINE DEFAULT STATIC_ONLY
  LINK_LIBRARIES ${rocksdb_static_libs}
)

ADD_SUBDIRECTORY(unittest)

MYSQL_ADD_EXECUTABLE(sst_dump ${CMAKE_SOURCE_DIR}/rocksdb/tools/sst_dump.cc)
TARGET_LINK_LIBRARIES(sst_dump rocksdb_se)

MYSQL_ADD_EXECUTABLE(ldb ${CMAKE_SOURCE_DIR}/rocksdb/tools/ldb.cc)
TARGET_LINK_LIBRARIES(ldb rocksdb_se)

