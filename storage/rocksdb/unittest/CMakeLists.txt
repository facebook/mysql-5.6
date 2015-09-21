IF (WITH_ROCKSDB_SE_STORAGE_ENGINE)
  INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/zlib
                      ${CMAKE_SOURCE_DIR}/unittest/mytap
    ${CMAKE_SOURCE_DIR}/rocksdb/third-party/gtest-1.7.0/fused-src
  )
  LINK_LIBRARIES(mytap mysys dbug strings)

  ADD_DEFINITIONS(-DSTANDALONE_UNITTEST)

  ADD_EXECUTABLE(test_rowlocks
          test_rowlocks.cc ../rdb_locks.cc ../rdb_locks.h)

  ADD_EXECUTABLE(test_rwlocks
          test_rwlocks.cc ../rdb_locks.cc ../rdb_locks.h)

  MYSQL_ADD_EXECUTABLE(test_properties_collector
          test_properties_collector.cc
          )
  TARGET_LINK_LIBRARIES(test_properties_collector mysqlserver)

  #MY_ADD_TEST(test_rowlocks)
ENDIF()
