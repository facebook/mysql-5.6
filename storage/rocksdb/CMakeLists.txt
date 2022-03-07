RETURN() # MyRocks is disabled

# TODO: Copyrights

# This is a strong requirement coming from RocksDB. No conditional checks here.
ADD_DEFINITIONS(-DROCKSDB_PLATFORM_POSIX -DROCKSDB_LIB_IO_POSIX)

IF(ROCKSDB_CUSTOM_NAMESPACE)
  ADD_DEFINITIONS(-DROCKSDB_NAMESPACE=${ROCKSDB_CUSTOM_NAMESPACE})
  ADD_DEFINITIONS(-DROCKSDB_CUSTOM_NAMESPACE=${ROCKSDB_CUSTOM_NAMESPACE})
ENDIF()

IF(HAVE_EXTERNAL_ROCKSDB)
  MESSAGE(STATUS "MyRocks: Using external RocksDB")
  SET(ROCKSDB_ROOT ${ROCKSDB_SRC_PATH})
ELSE()
  MESSAGE(STATUS "MyRocks: Using local RocksDB")

  IF (NOT EXISTS "${CMAKE_SOURCE_DIR}/rocksdb/Makefile")
    MESSAGE(SEND_ERROR "Missing Makefile in rocksdb directory. Try \"git submodule update\".")
  ENDIF()

  SET(ROCKSDB_ROOT ${CMAKE_SOURCE_DIR}/rocksdb)

  # get a list of rocksdb library source files
  # run with env -i to avoid passing variables
  EXECUTE_PROCESS(
    COMMAND env -i ${CMAKE_SOURCE_DIR}/storage/rocksdb/get_rocksdb_files.sh
    OUTPUT_VARIABLE SCRIPT_OUTPUT
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )

  # split the list into lines
  STRING(REGEX MATCHALL "[^\n]+" ROCKSDB_LIB_SOURCES ${SCRIPT_OUTPUT})

  SET(ROCKSDB_ROOT ${CMAKE_SOURCE_DIR}/rocksdb)

  # Statically include all RocksDB source
  SET(ROCKSDB_SOURCES
    ${ROCKSDB_LIB_SOURCES}
  )
ENDIF()

include(compiler_features)

IF (HAVE_SSE42)
  MESSAGE(STATUS "MyRocks: SSE42 support found")
  add_compile_flags(${ROCKSDB_ROOT}/util/crc32c.cc COMPILE_FLAGS "-msse4.2") # use Fast_CRC32
  IF (HAVE_PCLMUL)
    add_compile_flags(${ROCKSDB_ROOT}/util/crc32c.cc COMPILE_FLAGS "-mpclmul") # use crc32c_3way instead of Fast_CRC32
  ENDIF()
ELSE()
  MESSAGE(WARNING "No SSE42 support found, building MyRocks but without SSE42/FastCRC32 support")
ENDIF()

IF (HAVE_AVX2 AND NOT ROCKSDB_DISABLE_AVX2)
  add_compile_flags(${ROCKSDB_ROOT}/table/block_based/filter_policy.cc COMPILE_FLAGS "-mavx2")
ENDIF()

IF (HAVE_URING AND ROCKSDB_USE_IO_URING)
  SET(rocksdb_static_libs ${rocksdb_static_libs} ${URING_LIBRARY})
ENDIF()

IF (HAVE_MEMKIND AND NOT ROCKSDB_DISABLE_MEMKIND)
  SET(rocksdb_static_libs ${rocksdb_static_libs} ${MEMKIND_LIBRARY} -lnuma)
ENDIF()

IF (HAVE_ALIGNED_NEW AND NOT ROCKSDB_DISABLE_ALIGNED_NEW)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -faligned-new")
ENDIF()

IF (CMAKE_COMPILER_IS_GNUCXX)
  add_compile_options(-fno-builtin-memcmp)
ENDIF()

IF (CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "x86_64" AND NOT ROCKSDB_DISABLE_MARCH_NATIVE)
  add_compile_options(-march=native)
ENDIF()

IF(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_definitions(-DOS_LINUX)
ENDIF()

ROCKSDB_SET_DEFINTIONS()
get_directory_property(COMPILE_DEFINITIONS COMPILE_DEFINITIONS)
list(SORT COMPILE_DEFINITIONS)
message(STATUS "MyRocks compile definitions: ${COMPILE_DEFINITIONS}")


# Suppress warnings for all compilers
remove_compile_flags(-Wcast-qual -Wextra-semi)
append_cflags_if_supported(-Wno-invalid-offsetof)

# Suppress warnings for all clang versions
IF(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  remove_compile_flags(-Winconsistent-missing-destructor-override -Wdeprecated)
  ADD_CXX_COMPILE_FLAGS_TO_FILES(-Wno-shadow-field FILES ../../rocksdb/memtable/hash_skiplist_rep.cc)
  ADD_CXX_COMPILE_FLAGS_TO_FILES(-Wno-conditional-uninitialized FILES ../../rocksdb/env/fs_posix.cc)

  # platform007 (tbb, etc) has throw() which is deprecated
  append_cflags_if_supported(-Wno-deprecated-dynamic-exception-spec)
ENDIF()

# Suppress warnings for all gcc versions
IF(CMAKE_COMPILER_IS_GNUCXX)
  # "cmake/maintainer.cmake" sets "-Wmissing-format-attribute" which cause warnings with RocksDB
  remove_compile_flags(-Wmissing-format-attribute)
ENDIF()

# Suppress warnings for gcc-6.x or newer
IF(CMAKE_COMPILER_IS_GNUCXX AND NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS 6)
  ADD_CXX_COMPILE_FLAGS_TO_FILES(-Wno-logical-op FILES ../../rocksdb/file/filename.cc)
ENDIF()


INCLUDE_DIRECTORIES(
  ${ROCKSDB_ROOT}
  ${ROCKSDB_ROOT}/include
  ${ROCKSDB_ROOT}/third-party/gtest-1.8.1/fused-src
)

SET(ROCKSDB_SOURCES
  ${ROCKSDB_SOURCES}
  ha_rocksdb.cc ha_rocksdb.h ha_rocksdb_proto.h
  logger.h
  rdb_datadic.cc rdb_datadic.h
  rdb_cf_options.cc rdb_cf_options.h
  rdb_cf_manager.cc rdb_cf_manager.h
  rdb_converter.cc rdb_converter.h
  properties_collector.cc properties_collector.h
  event_listener.cc event_listener.h
  rdb_i_s.cc rdb_i_s.h
  rdb_index_merge.cc rdb_index_merge.h
  rdb_io_watchdog.cc rdb_io_watchdog.h
  rdb_perf_context.cc rdb_perf_context.h
  rdb_mutex_wrapper.cc rdb_mutex_wrapper.h
  rdb_psi.h rdb_psi.cc
  rdb_sst_info.cc rdb_sst_info.h
  rdb_utils.cc rdb_utils.h rdb_buff.h
  rdb_threads.cc rdb_threads.h
)

IF(WITH_FB_TSAN)
  SET(PIC_EXT "_pic")
ELSE()
  SET(PIC_EXT "")
ENDIF()

IF(HAVE_EXTERNAL_ROCKSDB)
  SET(rocksdb_static_libs ${rocksdb_static_libs} "${ROCKSDB_LIB_PATH}/${ROCKSDB_LIB_NAME}")
ENDIF()

IF (WITH_JEMALLOC)
  SET(rocksdb_static_libs ${rocksdb_static_libs} -Wl,-rpath=$ORIGIN
    ${JEMALLOC_LIBRARY})
  ADD_DEFINITIONS(-DROCKSDB_JEMALLOC)
  ADD_DEFINITIONS(-DROCKSDB_MALLOC_USABLE_SIZE)
ENDIF()

IF (WITH_UNWIND)
  FIND_LIBRARY(UNWIND_LIBRARY
    NAMES libunwind${PIC_EXT}.a unwind
    HINTS ${WITH_UNWIND}/lib)
  SET(rocksdb_static_libs ${rocksdb_static_libs}
    ${UNWIND_LIBRARY})
ENDIF()

IF (WITH_SNAPPY)
  FIND_LIBRARY(SNAPPY_LIBRARY
    NAMES libsnappy${PIC_EXT}.a snappy
    HINTS ${WITH_SNAPPY}/lib)
  SET(rocksdb_static_libs ${rocksdb_static_libs}
    ${SNAPPY_LIBRARY})
  ADD_DEFINITIONS(-DSNAPPY)
ENDIF()

IF (WITH_LZ4)
  FIND_LIBRARY(LZ4_LIBRARY
    NAMES liblz4${PIC_EXT}.a lz4
    HINTS ${WITH_LZ4}/lib)
  SET(rocksdb_static_libs ${rocksdb_static_libs}
    ${LZ4_LIBRARY})
  ADD_DEFINITIONS(-DLZ4)
ENDIF()

IF (WITH_BZ2)
  FIND_LIBRARY(BZ2_LIBRARY
    NAMES libbz2${PIC_EXT}.a bz2
    HINTS ${WITH_BZ2}/lib)
  SET(rocksdb_static_libs ${rocksdb_static_libs}
    ${BZ2_LIBRARY})
  ADD_DEFINITIONS(-DBZIP2)
ENDIF()

IF (WITH_ZSTD)
  SET(rocksdb_static_libs ${rocksdb_static_libs} ${ZSTD_LIBRARY})
  ADD_DEFINITIONS(-DZSTD)
  ADD_DEFINITIONS(-DZSTD_STATIC_LINKING_ONLY)
ENDIF()

IF (WITH_TBB)
  FIND_LIBRARY(TBB_LIBRARY
    NAMES libtbb${PIC_EXT}.a tbb
    HINTS ${WITH_TBB}/lib)
  SET(rocksdb_static_libs ${rocksdb_static_libs}
    ${TBB_LIBRARY})
  ADD_DEFINITIONS(-DTBB)
ENDIF()

CHECK_INCLUDE_FILES(zlib.h HAVE_ZLIB_H)

IF (HAVE_ZLIB_H)
  ADD_DEFINITIONS(-DZLIB)
  SET(rocksdb_static_libs ${rocksdb_static_libs} ${ZLIB_LIBRARY})
ENDIF()

SET(rocksdb_static_libs ${rocksdb_static_libs} "-lrt" "-ldl" "-lpthread")

MYSQL_ADD_PLUGIN(rocksdb_se ${ROCKSDB_SOURCES} STORAGE_ENGINE DEFAULT STATIC_ONLY
  LINK_LIBRARIES ${rocksdb_static_libs}
)

IF(WITH_EMBEDDED_SERVER)
  ADD_SUBDIRECTORY(unittest)
ENDIF()

IF (WITH_ROCKSDB_SE_STORAGE_ENGINE AND (NOT HAVE_EXTERNAL_ROCKSDB))
  # TODO: read this file list from src.mk:TOOL_SOURCES
  SET(ROCKSDB_TOOL_SOURCES
    ${CMAKE_SOURCE_DIR}/rocksdb/tools/ldb_tool.cc
    ${CMAKE_SOURCE_DIR}/rocksdb/tools/ldb_cmd.cc
    ${CMAKE_SOURCE_DIR}/rocksdb/tools/sst_dump_tool.cc
  )
  MYSQL_ADD_EXECUTABLE(sst_dump ${CMAKE_SOURCE_DIR}/rocksdb/tools/sst_dump.cc ${ROCKSDB_TOOL_SOURCES})
  TARGET_LINK_LIBRARIES(sst_dump rocksdb_se)

  MYSQL_ADD_EXECUTABLE(ldb ${CMAKE_SOURCE_DIR}/rocksdb/tools/ldb.cc ${ROCKSDB_TOOL_SOURCES})
  TARGET_LINK_LIBRARIES(ldb rocksdb_se)

  MYSQL_ADD_EXECUTABLE(mysql_ldb ${CMAKE_SOURCE_DIR}/storage/rocksdb/tools/mysql_ldb.cc ${ROCKSDB_TOOL_SOURCES})
  TARGET_LINK_LIBRARIES(mysql_ldb rocksdb_se)
ENDIF()
