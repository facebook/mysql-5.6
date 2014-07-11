
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/zlib
                    ${CMAKE_SOURCE_DIR}/unittest/mytap)
LINK_LIBRARIES(mytap mysys dbug strings)

ADD_DEFINITIONS(-DSTANDALONE_UNITTEST)

ADD_EXECUTABLE(test_rowlocks
        test_rowlocks.cc ../rdb_locks.cc ../rdb_locks.h)
#MY_ADD_TEST(test_rowlocks)
