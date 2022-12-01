# Copyright (c) 2020, Percona and/or its affiliates. All rights reserved.
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
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */


# Functions to detect features supported by compiler

include(CheckCXXSourceRuns)
include(CheckCXXSourceCompiles)
include(CheckCXXSymbolExists)


# usage: CHECK_X86_CPU_FEATURE(sse4.2 HAVE_SSE42)
# it ignores -march=ARCH
function (CHECK_X86_CPU_FEATURE FEATURE PARAM_OUT)
  set(CMAKE_REQUIRED_FLAGS "-Wno-error")
  CHECK_CXX_SOURCE_RUNS("
  int main() {
    return !__builtin_cpu_supports(\"${FEATURE}\");
  }
  " HAVE_X86_CPU_FEATURE_${FEATURE})
  unset(CMAKE_REQUIRED_FLAGS)
  set(${PARAM_OUT} ${HAVE_X86_CPU_FEATURE_${FEATURE}} PARENT_SCOPE)
endfunction (CHECK_X86_CPU_FEATURE)


# usage: CHECK_VARIABLE_DEFINED(__AVX2__ HAVE_AVX2)
# it depends on -march=ARCH
function (CHECK_VARIABLE_DEFINED VARIABLE PARAM_OUT)
  set(CMAKE_REQUIRED_FLAGS "-Wno-error")
  CHECK_CXX_SOURCE_COMPILES("
  #if !defined(${VARIABLE})
  #error ${VARIABLE} not defined
  #endif
  int main() {
    return 0;
  }
  " CHECK_VARIABLE_DEFINED_${VARIABLE})
  unset(CMAKE_REQUIRED_FLAGS)
  set(${PARAM_OUT} ${CHECK_VARIABLE_DEFINED_${VARIABLE}} PARENT_SCOPE)
endfunction (CHECK_VARIABLE_DEFINED)


function (CHECK_ALIGNED_NEW_SUPPORT PARAM_OUT)
  set(CMAKE_REQUIRED_FLAGS "-faligned-new -Wno-error")
  CHECK_CXX_SOURCE_RUNS("
  struct alignas(1024) t {int a;};
  int main() { return 0; }
  " HAVE_ALIGNED_NEW)
  unset(CMAKE_REQUIRED_FLAGS)
  set(${PARAM_OUT} ${HAVE_ALIGNED_NEW} PARENT_SCOPE)
endfunction (CHECK_ALIGNED_NEW_SUPPORT)


function (CHECK_UINT128_EXTENSION_SUPPORT PARAM_OUT)
  set(CMAKE_REQUIRED_FLAGS "--std=c++17 -Wno-error")
  CHECK_CXX_SOURCE_RUNS("
  #include <cstdint>
  int main() {
    uint64_t a = 0xffffFFFFffffFFFF;
    __uint128_t b = __uint128_t(a) * a;
    a = static_cast<uint64_t>(b >> 64);
    return 0;
  }
  " HAVE_UINT128_EXTENSION)
  unset(CMAKE_REQUIRED_FLAGS)
  set(${PARAM_OUT} ${HAVE_UINT128_EXTENSION} PARENT_SCOPE)
endfunction (CHECK_UINT128_EXTENSION_SUPPORT)


function (CHECK_URING_SUPPORT PARAM_OUT)
  set(CMAKE_REQUIRED_FLAGS "${CMAKE_CXX_LINK_FLAGS} -Wno-error")
  FIND_LIBRARY(URING_LIBRARY NAMES liburing.a)
  IF (URING_LIBRARY)
    set(CMAKE_REQUIRED_LIBRARIES ${URING_LIBRARY})
    CHECK_CXX_SOURCE_RUNS("
    #include <liburing.h>
    int main() {
      struct io_uring ring;
      io_uring_queue_init(1, &ring, 0);
      return 0;
    }
    " HAVE_URING)
    unset(CMAKE_REQUIRED_LIBRARIES)
  ENDIF()
  unset(CMAKE_REQUIRED_FLAGS)
  set(${PARAM_OUT} ${HAVE_URING} PARENT_SCOPE)
endfunction (CHECK_URING_SUPPORT)


function (CHECK_MEMKIND_SUPPORT PARAM_OUT)
  set(CMAKE_REQUIRED_FLAGS "-Wno-error")
  FIND_LIBRARY(MEMKIND_LIBRARY NAMES libmemkind.a)
  IF (MEMKIND_LIBRARY)
    set(CMAKE_REQUIRED_LIBRARIES ${MEMKIND_LIBRARY} -ldl -lpthread -lnuma)
    CHECK_CXX_SOURCE_RUNS("
    #include <memkind.h>
    int main() {
      memkind_malloc(MEMKIND_DAX_KMEM, 1024);
      return 0;
    }
    " HAVE_MEMKIND)
    unset(CMAKE_REQUIRED_LIBRARIES)
  ENDIF()
  unset(CMAKE_REQUIRED_FLAGS)
  set(${PARAM_OUT} ${HAVE_MEMKIND} PARENT_SCOPE)
endfunction (CHECK_MEMKIND_SUPPORT)


function (CHECK_THREAD_LOCAL_SUPPORT PARAM_OUT)
  set(CMAKE_REQUIRED_FLAGS "-Wno-error")
  CHECK_CXX_SOURCE_RUNS("
  #if defined(_MSC_VER) && !defined(__thread)
  #define __thread __declspec(thread)
  #endif
  int main() {
    static __thread int tls;
    return 0;
  }
  " HAVE_THREAD_LOCAL)
  unset(CMAKE_REQUIRED_FLAGS)
  set(${PARAM_OUT} ${HAVE_THREAD_LOCAL} PARENT_SCOPE)
endfunction (CHECK_THREAD_LOCAL_SUPPORT)


function (CHECK_FALLOCATE_SUPPORT PARAM_OUT)
  set(CMAKE_REQUIRED_FLAGS "-Wno-error")
  CHECK_CXX_SOURCE_RUNS("
  #include <fcntl.h>
  #include <linux/falloc.h>
  int main() {
    int fd = open(\"/dev/null\", 0);
    fallocate(fd, FALLOC_FL_KEEP_SIZE, 0, 1024);
    return 0;
  }
  " HAVE_FALLOCATE)
  unset(CMAKE_REQUIRED_FLAGS)
  set(${PARAM_OUT} ${HAVE_FALLOCATE} PARENT_SCOPE)
endfunction (CHECK_FALLOCATE_SUPPORT)


function (CHECK_PTHREAD_MUTEX_ADAPTIVE_NP_SUPPORT PARAM_OUT)
  set(CMAKE_REQUIRED_FLAGS "-Wno-error")
  CHECK_CXX_SOURCE_RUNS("
  #include <pthread.h>
  int main() {
    int x = PTHREAD_MUTEX_ADAPTIVE_NP;
    return 0;
  }
  " HAVE_PTHREAD_MUTEX_ADAPTIVE_NP)
  unset(CMAKE_REQUIRED_FLAGS)
  set(${PARAM_OUT} ${HAVE_PTHREAD_MUTEX_ADAPTIVE_NP} PARENT_SCOPE)
endfunction (CHECK_PTHREAD_MUTEX_ADAPTIVE_NP_SUPPORT)


function (CHECK_BACKTRACE_SYMBOLS_SUPPORT PARAM_OUT)
  set(CMAKE_REQUIRED_FLAGS "-Wno-error")
  CHECK_CXX_SOURCE_RUNS("
  #include <pthread.h>
  #include <execinfo.h>
  int main() {
    void* frames[1];
    backtrace_symbols(frames, backtrace(frames, 1));
    return 0;
  }
  " HAVE_BACKTRACE_SYMBOLS)
  unset(CMAKE_REQUIRED_FLAGS)
  set(${PARAM_OUT} ${HAVE_BACKTRACE_SYMBOLS} PARENT_SCOPE)
endfunction (CHECK_BACKTRACE_SYMBOLS_SUPPORT)


function (CHECK_SYNC_FILE_RANGE_WRITE_SUPPORT PARAM_OUT)
  set(CMAKE_REQUIRED_FLAGS "-Wno-error")
  CHECK_CXX_SOURCE_RUNS("
  #include <fcntl.h>
  int main() {
    int fd = open(\"/dev/null\", 0);
    sync_file_range(fd, 0, 1024, SYNC_FILE_RANGE_WRITE);
    return 0;
  }
  " HAVE_SYNC_FILE_RANGE_WRITE)
  unset(CMAKE_REQUIRED_FLAGS)
  set(${PARAM_OUT} ${HAVE_SYNC_FILE_RANGE_WRITE} PARENT_SCOPE)
endfunction (CHECK_SYNC_FILE_RANGE_WRITE_SUPPORT)


macro (ROCKSDB_SET_X86_DEFINTIONS)
  CHECK_VARIABLE_DEFINED(__SSE4_2__ HAVE_SSE42)
  if (HAVE_SSE42)
    add_definitions(-DHAVE_SSE42)
  else()
    IF (ALLOW_NO_SSE42)
      MESSAGE(WARNING "No SSE42 support found and ALLOW_NO_SSE42 specified, building MyRocks but without SSE42/FastCRC32 support")
    ELSE()
      MESSAGE(FATAL_ERROR "No SSE42 support found. Not building MyRocks. Set ALLOW_NO_SSE42 to build MyRocks with slow CRC32.")
    ENDIF()
  endif()

  CHECK_VARIABLE_DEFINED(__PCLMUL__ HAVE_PCLMUL)
  if (HAVE_PCLMUL)
    add_definitions(-DHAVE_PCLMUL)
  endif()

  CHECK_VARIABLE_DEFINED(__AVX2__ HAVE_AVX2)
  if (HAVE_AVX2 AND NOT ROCKSDB_DISABLE_AVX2)
    add_definitions(-DHAVE_AVX2)
  endif()

  CHECK_VARIABLE_DEFINED(__AVX512F__ HAVE_AVX512F)
endmacro (ROCKSDB_SET_X86_DEFINTIONS)


macro (ROCKSDB_SET_ARM64_DEFINTIONS)
  CHECK_VARIABLE_DEFINED(__ARM_FEATURE_CRC32  HAVE_ARMV8_CRC)
  CHECK_VARIABLE_DEFINED(__ARM_FEATURE_CRYPTO HAVE_ARMV8_CRYPTO)

  IF (NOT HAVE_ARMV8_CRC OR NOT HAVE_ARMV8_CRYPTO)
    IF (ALLOW_NO_ARMV81A_CRYPTO)
      MESSAGE(WARNING "No ARMv8.1-A+crypto support found and ALLOW_NO_ARMV81A_CRYPTO specified, building MyRocks but without ARMv8.1-A+crypto/FastCRC32 support")
    ELSE()
      MESSAGE(FATAL_ERROR "No ARMv8.1-A+crypto support found. Not building MyRocks. Set ALLOW_NO_ARMV81A_CRYPTO to build MyRocks with slow CRC32.")
    ENDIF()
  ENDIF()
endmacro (ROCKSDB_SET_ARM64_DEFINTIONS)


macro (ROCKSDB_SET_BUILD_ARCHITECTURE)
  IF (CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "x86_64")
    IF (NOT ROCKSDB_BUILD_ARCH)
      IF (ROCKSDB_DISABLE_MARCH_NATIVE)
        # Intel Westmere is oldest CPU that supports SSE 4.2 and PCLMUL instruction sets
        # the compiled binary will also work on "core2" and "nehalem" because "sse4.2" and "pclmul" are checked on runtime
        SET(ROCKSDB_BUILD_ARCH "westmere")
      ELSE()
        SET(ROCKSDB_BUILD_ARCH "native")
      ENDIF()
      MESSAGE(STATUS "MyRocks x86_64 build architecture: -march=${ROCKSDB_BUILD_ARCH}")
    ELSE()
      MESSAGE(STATUS "MyRocks x86_64 build architecture: -march=${ROCKSDB_BUILD_ARCH}. If the enforced processor architecture is newer than the processor architecture of this build machine, the created binary may crash if executed on this machine.")
    ENDIF()

    # CMAKE_CXX_FLAGS are used by CHECK_CXX_SOURCE_COMPILES called from ROCKSDB_SET_X86_DEFINTIONS()
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=${ROCKSDB_BUILD_ARCH}")
    ROCKSDB_SET_X86_DEFINTIONS()
  ELSE() # aarch64
    IF (NOT ROCKSDB_BUILD_ARCH)
      IF (ROCKSDB_DISABLE_MARCH_NATIVE)
        # minimal required platform is armv8.1-a (with "+lse" and "+crc"); support for "+crypto" is checked on runtime
        SET(ROCKSDB_BUILD_ARCH "armv8.1-a+crypto")
      ELSE()
        IF(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
          IF (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 15) # clang-14 or older (doesn't support "-march=native")
            SET(ROCKSDB_BUILD_ARCH "armv8.1-a+crypto")
          ELSE()
            # always compile binary with "+crypto" because support for "+crypto" is checked on runtime
            SET(ROCKSDB_BUILD_ARCH "native+crypto")
          ENDIF()
        ELSE()
          SET(ROCKSDB_BUILD_ARCH "native")
        ENDIF()
      ENDIF()
      MESSAGE(STATUS "MyRocks arm64 build architecture: -march=${ROCKSDB_BUILD_ARCH}")
    ELSE()
      MESSAGE(STATUS "MyRocks arm64 build architecture: -march=${ROCKSDB_BUILD_ARCH}. If the enforced processor architecture is newer than the processor architecture of this build machine, the created binary may crash if executed on this machine.")
    ENDIF()

    # CMAKE_CXX_FLAGS are used by CHECK_CXX_SOURCE_COMPILES called from ROCKSDB_SET_ARM64_DEFINTIONS()
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=${ROCKSDB_BUILD_ARCH}")
    ROCKSDB_SET_ARM64_DEFINTIONS()
  ENDIF()
endmacro (ROCKSDB_SET_BUILD_ARCHITECTURE)


macro (ROCKSDB_SET_DEFINTIONS)
  CHECK_ALIGNED_NEW_SUPPORT(HAVE_ALIGNED_NEW)
  if (HAVE_ALIGNED_NEW AND NOT ROCKSDB_DISABLE_ALIGNED_NEW)
    add_definitions(-DHAVE_ALIGNED_NEW)
  endif()

  CHECK_UINT128_EXTENSION_SUPPORT(HAVE_UINT128_EXTENSION)
  if (HAVE_UINT128_EXTENSION)
    add_definitions(-DHAVE_UINT128_EXTENSION)
  endif()

  CHECK_URING_SUPPORT(HAVE_URING)
  if (HAVE_URING)
    if (ROCKSDB_USE_IO_URING)
       add_definitions(-DROCKSDB_IOURING_PRESENT)
     endif()
  endif()

  CHECK_MEMKIND_SUPPORT(HAVE_MEMKIND)
  if (HAVE_MEMKIND)
    if (ROCKSDB_USE_MEMKIND)
       add_definitions(-DMEMKIND)
     endif()
  endif()

  CHECK_THREAD_LOCAL_SUPPORT(HAVE_THREAD_LOCAL)
  if (HAVE_THREAD_LOCAL)
    add_definitions(-DROCKSDB_SUPPORT_THREAD_LOCAL)
  else()
    MESSAGE(FATAL_ERROR "No '__thread' support found. Not building MyRocks")
  endif()

  CHECK_FALLOCATE_SUPPORT(HAVE_FALLOCATE)
  if (HAVE_FALLOCATE AND NOT ROCKSDB_DISABLE_FALLOCATE)
    add_definitions(-DROCKSDB_FALLOCATE_PRESENT)
  endif()

  CHECK_PTHREAD_MUTEX_ADAPTIVE_NP_SUPPORT(HAVE_PTHREAD_MUTEX_ADAPTIVE_NP)
  if (HAVE_PTHREAD_MUTEX_ADAPTIVE_NP AND NOT ROCKSDB_DISABLE_PTHREAD_MUTEX_ADAPTIVE_NP)
    add_definitions(-DROCKSDB_PTHREAD_ADAPTIVE_MUTEX)
  endif()

  CHECK_BACKTRACE_SYMBOLS_SUPPORT(HAVE_BACKTRACE_SYMBOLS)
  if (HAVE_BACKTRACE_SYMBOLS AND NOT ROCKSDB_DISABLE_BACKTRACE)
    add_definitions(-DROCKSDB_BACKTRACE)
  endif()

  CHECK_SYNC_FILE_RANGE_WRITE_SUPPORT(HAVE_SYNC_FILE_RANGE_WRITE)
  if (HAVE_SYNC_FILE_RANGE_WRITE AND NOT ROCKSDB_DISABLE_SYNC_FILE_RANGE)
    add_definitions(-DROCKSDB_RANGESYNC_PRESENT)
  endif()

  if (CMAKE_SYSTEM_NAME MATCHES "^FreeBSD")
    check_cxx_symbol_exists(malloc_usable_size malloc_np.h HAVE_MALLOC_USABLE_SIZE)
  else()
    check_cxx_symbol_exists(malloc_usable_size malloc.h HAVE_MALLOC_USABLE_SIZE)
  endif()
  if (HAVE_MALLOC_USABLE_SIZE)
    if (ROCKSDB_USE_MALLOC_USABLE_SIZE)
       add_definitions(-DROCKSDB_MALLOC_USABLE_SIZE)
     endif()
  endif()

  check_cxx_symbol_exists(sched_getcpu sched.h ROCKSDB_SCHED_GETCPU_PRESENT)
  if (ROCKSDB_SCHED_GETCPU_PRESENT AND NOT ROCKSDB_DISABLE_SCHED_GETCPU)
    add_definitions(-DROCKSDB_SCHED_GETCPU_PRESENT -DHAVE_SCHED_GETCPU=1)
  endif()

  check_cxx_symbol_exists(getauxval sys/auxv.h HAVE_AUXV_GETAUXVAL)
  if (HAVE_AUXV_GETAUXVAL AND NOT ROCKSDB_DISABLE_AUXV_GETAUXVAL)
    add_definitions(-DROCKSDB_AUXV_GETAUXVAL_PRESENT)
  endif()
endmacro()
