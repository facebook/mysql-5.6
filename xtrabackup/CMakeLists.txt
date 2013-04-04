CMAKE_MINIMUM_REQUIRED(VERSION 2.6)
IF("${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}" GREATER 2.6)
  CMAKE_POLICY(VERSION 2.8)
ENDIF()

IF (NOT DEFINED MYSQL_ROOT_DIR)
  MESSAGE(FATAL_ERROR
    "You must specify a path to the server source directory with
 -DMYSQL_ROOT_DIR")
ENDIF()

if(COMMAND cmake_policy)
   cmake_policy(SET CMP0015 NEW)
endif(COMMAND cmake_policy)

LINK_DIRECTORIES (
  ${MYSQL_ROOT_DIR}/mysys/RelWithDebInfo
  ${MYSQL_ROOT_DIR}/strings/RelWithDebInfo
  ${MYSQL_ROOT_DIR}/storage/innobase/RelWithDebInfo
  ${MYSQL_ROOT_DIR}/zlib/RelWithDebInfo
  ${MYSQL_ROOT_DIR}/regex/RelWithDebInfo
)

PROJECT(XtraBackup)
ADD_EXECUTABLE(xtrabackup
  src/xtrabackup.cc
  src/xb_regex.h
)

if(COMMAND cmake_policy)
   cmake_policy(SET CMP0016 NEW)
endif(COMMAND cmake_policy)

# Enable debug info also in Release build, and create PDB to be able to
# analyze crashes
SET(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Zi")
SET(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /debug")

# Force static runtime libraries
FOREACH(flag
    CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_RELWITHDEBINFO
    CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_DEBUG_INIT
    CMAKE_CXX_FLAGS_RELEASE  CMAKE_CXX_FLAGS_RELWITHDEBINFO
    CMAKE_CXX_FLAGS_DEBUG  CMAKE_CXX_FLAGS_DEBUG_INIT)
  STRING(REPLACE "/MD"  "/MT" "${flag}" "${${flag}}")
ENDFOREACH()

# Silence "deprecated API" warnings.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd4996")

INCLUDE_DIRECTORIES(
  ${MYSQL_ROOT_DIR}/include
  ${MYSQL_ROOT_DIR}/regex
  ${MYSQL_ROOT_DIR}/storage/innobase/include
  ${MYSQL_ROOT_DIR}/zlib
)

TARGET_LINK_LIBRARIES(xtrabackup mysys strings innobase zlib regex)
ADD_DEFINITIONS(-DDBUG_OFF -DHAVE_CONFIG_H -DWIN_ASYNC_IO
  -DHAVE_WINDOWS_ATOMICS)
