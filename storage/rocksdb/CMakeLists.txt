# TODO: Copyrights

IF(WITHOUT_ROCKSDB_STORAGE_ENGINE)
  RETURN()
ENDIF()

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

  # We assume that make checkout_folly has already been run before attempting
  # to build with folly.
  IF(ROCKSDB_FOLLY)
    IF (NOT EXISTS "${CMAKE_SOURCE_DIR}/rocksdb/third-party/folly")
      MESSAGE(SEND_ERROR "Missing folly in rocksdb directory. Try \"make checkout_folly\".")
    ENDIF()
    ADD_DEFINITIONS(-DUSE_FOLLY -DFOLLY_NO_CONFIG)
    INCLUDE_DIRECTORIES(
      ${ROCKSDB_ROOT}/third-party/folly
    )
    IF (WITH_GLOG)
      FIND_LIBRARY(GLOG_LIBRARY
        NAMES libglog${PIC_EXT}.a glog
        HINTS ${WITH_GLOG}/lib)
      SET(rocksdb_static_libs ${rocksdb_static_libs}
        ${GLOG_LIBRARY})
    ENDIF()
    IF (WITH_GFLAGS)
      FIND_LIBRARY(GFLAGS_LIBRARY
        NAMES libgflags${PIC_EXT}.a gflags
        HINTS ${WITH_GFLAGS}/lib)
      SET(rocksdb_static_libs ${rocksdb_static_libs}
        ${GFLAGS_LIBRARY})
    ENDIF()
  ENDIF()
  # get a list of rocksdb library source files
  # run with env -i to avoid passing variables
  EXECUTE_PROCESS(
    COMMAND env -i CXX=${CMAKE_CXX_COMPILER}
    ${CMAKE_SOURCE_DIR}/storage/rocksdb/get_rocksdb_files.sh ${ROCKSDB_FOLLY}
    OUTPUT_VARIABLE SCRIPT_OUTPUT
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )

  # split the list into lines
  STRING(REGEX MATCHALL "[^\n]+" ROCKSDB_LIB_SOURCES ${SCRIPT_OUTPUT})

  SET(ROCKSDB_ROOT ${CMAKE_SOURCE_DIR}/rocksdb)
ENDIF()

IF (WITH_UBSAN)
  ADD_DEFINITIONS(-DROCKSDB_UBSAN_RUN)
ENDIF()
IF (WITH_VALGRIND)
  ADD_DEFINITIONS(-DROCKSDB_VALGRIND_RUN)
ENDIF()
IF (WITH_NUMA)
  ADD_DEFINITIONS(-DNUMA)
ENDIF()

IF (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  ADD_DEFINITIONS(-DOS_LINUX)
ELSEIF(CMAKE_SYSTEM_NAME MATCHES "Darwin")
  ADD_DEFINITIONS(-DOS_MACOSX)
ENDIF()

IF (CMAKE_COMPILER_IS_GNUCXX)
  ADD_COMPILE_OPTIONS(-fno-builtin-memcmp)
ENDIF()


INCLUDE(compiler_features)
ROCKSDB_SET_BUILD_ARCHITECTURE()  # sets "-march=" using ROCKSDB_BUILD_ARCH or ROCKSDB_DISABLE_MARCH_NATIVE
ROCKSDB_SET_DEFINTIONS()          # sets HAVE_MEMKIND, HAVE_ALIGNED_NEW

IF (HAVE_MEMKIND AND NOT ROCKSDB_DISABLE_MEMKIND)
  SET(rocksdb_static_libs ${rocksdb_static_libs} ${MEMKIND_LIBRARY} -lnuma)
ENDIF()

IF (HAVE_ALIGNED_NEW AND NOT ROCKSDB_DISABLE_ALIGNED_NEW)
  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -faligned-new")
ENDIF()

GET_DIRECTORY_PROPERTY(COMPILE_DEFINITIONS COMPILE_DEFINITIONS)
LIST(SORT COMPILE_DEFINITIONS)
MESSAGE(STATUS "MyRocks compile definitions: ${COMPILE_DEFINITIONS}")


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

# Suppress warnings for clang-15 or newer
IF(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS 15)
  ADD_CXX_COMPILE_FLAGS_TO_FILES(-Wno-unused-but-set-variable FILES ../../rocksdb/db/version_edit_handler.cc ../../rocksdb/cache/lru_cache.cc)
  ADD_CXX_COMPILE_FLAGS_TO_FILES(-Wno-array-parameter FILES ha_rocksdb.cc)
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

# Suppress warnings for gcc-12 or newer
IF(CMAKE_COMPILER_IS_GNUCXX AND NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS 12)
  IF(HAVE_AVX512F)
    MESSAGE(STATUS "AVX-512: add -Wno-uninitialized for xxhash.cc (check https://gcc.gnu.org/bugzilla/show_bug.cgi?id=105593)")
    ADD_CXX_COMPILE_FLAGS_TO_FILES(-Wno-uninitialized -Wno-maybe-uninitialized FILES ../../rocksdb/util/xxhash.cc ${BUNDLED_LZ4_PATH}/xxhash.c)
  ENDIF()
ENDIF()

IF(FB_WITH_WSENV)
  ADD_DEFINITIONS(-DFB_HAVE_WSENV=1)
  INCLUDE_DIRECTORIES(
    ${FB_WITH_WSENV}
  )
ENDIF()

INCLUDE_DIRECTORIES(
  ${ROCKSDB_ROOT}
  ${ROCKSDB_ROOT}/include
  ${ROCKSDB_ROOT}/third-party/gtest-1.8.1/fused-src
)

SET(ROCKSDB_SE_SOURCES
  ha_rocksdb.cc ha_rocksdb.h ha_rocksdb_proto.h
  logger.h
  rdb_datadic.cc rdb_datadic.h
  rdb_iterator.cc rdb_iterator.h
  rdb_sst_partitioner_factory.h
  rdb_cf_options.cc rdb_cf_options.h
  rdb_cf_manager.cc rdb_cf_manager.h
  rdb_converter.cc rdb_converter.h
  rdb_native_dd.cc rdb_native_dd.h
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
  rdb_nosql_digest.cc rdb_nosql_digest.h
  nosql_access.cc nosql_access.h
  ha_rockspart.cc ha_rockspart.h
  sql_dd.cc sql_dd.h
  rdb_cmd_srv_helper.cc rdb_cmd_srv_helper.h
  rdb_vector_db.cc rdb_vector_db.h
  clone/common.h clone/common.cc clone/donor.h clone/donor.cc clone/client.cc
)

IF(WITH_FB_TSAN OR ROCKSDB_DYNAMIC_PLUGIN)
  SET(PIC_EXT "_pic")
ELSE()
  SET(PIC_EXT "")
ENDIF()

IF(HAVE_EXTERNAL_ROCKSDB)
  SET(rocksdb_static_libs ${rocksdb_static_libs} "${ROCKSDB_LIB_PATH}/${ROCKSDB_LIB_NAME}")
ENDIF()

IF (WITH_JEMALLOC)
  # Not static linking jemalloc with a dynamic plugin to avoid
  # "cannot allocate memory in static TLS block" error
  IF(NOT ROCKSDB_DYNAMIC_PLUGIN)
    SET(rocksdb_static_libs ${rocksdb_static_libs} -Wl,-rpath=$ORIGIN
      ${JEMALLOC_LIBRARY})
  ENDIF()
  ADD_DEFINITIONS(-DROCKSDB_JEMALLOC)
  IF (HAVE_MALLOC_USABLE_SIZE)
    ADD_DEFINITIONS(-DROCKSDB_MALLOC_USABLE_SIZE)
  ENDIF()
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

IF (WITH_LZ4)
  SET(rocksdb_static_libs ${rocksdb_static_libs} ${LZ4_LIBRARY})
  ADD_DEFINITIONS(-DLZ4)
ENDIF()

IF (WITH_LIBURING)
  FIND_LIBRARY(URING_LIBRARY
    NAMES liburing${PIC_EXT}.a uring
    HINTS ${WITH_LIBURING}/lib)
  SET(rocksdb_static_libs ${rocksdb_static_libs}
    ${URING_LIBRARY})
  ADD_DEFINITIONS(-DROCKSDB_USE_IO_URING=1)
  ADD_DEFINITIONS(-DROCKSDB_IOURING_PRESENT)
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

IF (WITH_FB_VECTORDB)
  MESSAGE(STATUS "Building myrocks with fb vectordb support")
  INCLUDE_DIRECTORIES("${FAISS_SOURCE_DIR}")
  # Suppress warnings brought by faiss header files
  ADD_CXX_COMPILE_FLAGS_TO_FILES(-Wno-shadow-field FILES rdb_vector_db.cc rdb_vector_db.h)
  ADD_CXX_COMPILE_FLAGS_TO_FILES(-Wno-unused-parameter FILES rdb_vector_db.cc rdb_vector_db.h)
ENDIF()

IF (HAVE_ZLIB_H)
  ADD_DEFINITIONS(-DZLIB)
  SET(rocksdb_static_libs ${rocksdb_static_libs} ${ZLIB_LIBRARY})
ENDIF()

SET(rocksdb_static_libs ${rocksdb_static_libs} "-ldl" "-lpthread")
IF(NOT CMAKE_SYSTEM_NAME MATCHES "Darwin")
  SET(rocksdb_static_libs ${rocksdb_static_libs} "-lrt")
ENDIF()

ADD_LIBRARY(rocksdb_sources ${ROCKSDB_LIB_SOURCES})
TARGET_LINK_LIBRARIES(rocksdb_sources ${rocksdb_static_libs})

IF (ROCKSDB_DYNAMIC_PLUGIN)
  MYSQL_ADD_PLUGIN(rocksdb ${ROCKSDB_SE_SOURCES} STORAGE_ENGINE DEFAULT
    MODULE_ONLY LINK_LIBRARIES rocksdb_sources)
ELSE()
  MYSQL_ADD_PLUGIN(rocksdb_se ${ROCKSDB_SE_SOURCES} STORAGE_ENGINE MANDATORY
    STATIC_ONLY LINK_LIBRARIES rocksdb_sources)
    # to fix linkage error on partition_base
    TARGET_LINK_LIBRARIES(rocksdb_se sql_main)
ENDIF()

IF(WITH_EMBEDDED_SERVER)
  ADD_SUBDIRECTORY(unittest)
ENDIF()

IF (NOT HAVE_EXTERNAL_ROCKSDB)
  # TODO: read this file list from src.mk:TOOL_SOURCES
  ADD_LIBRARY(rocksdb_tool
    ${CMAKE_SOURCE_DIR}/rocksdb/tools/ldb_tool.cc
    ${CMAKE_SOURCE_DIR}/rocksdb/tools/ldb_cmd.cc
    ${CMAKE_SOURCE_DIR}/rocksdb/tools/sst_dump_tool.cc
    ${CMAKE_SOURCE_DIR}/rocksdb/utilities/blob_db/blob_dump_tool.cc
  )
  TARGET_LINK_LIBRARIES(rocksdb_tool rocksdb_sources)

  MYSQL_ADD_EXECUTABLE(sst_dump ${CMAKE_SOURCE_DIR}/rocksdb/tools/sst_dump.cc)
  TARGET_LINK_LIBRARIES(sst_dump rocksdb_tool)

  MYSQL_ADD_EXECUTABLE(ldb ${CMAKE_SOURCE_DIR}/rocksdb/tools/ldb.cc)
  TARGET_LINK_LIBRARIES(ldb rocksdb_tool)

ENDIF()
