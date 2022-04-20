#include <gtest/gtest.h>

namespace junit_integration_unittest {

// Configure with -DWITH_FAILING_GUNIT_TESTS=ON to enable this test.

TEST(JUnitIntegrationTest, Pass) { EXPECT_EQ(1, 1); }

TEST(JUnitIntegrationTest, SimpleFailure) { EXPECT_EQ(3, 5); }

TEST(JUnitIntegrationTest, Crash) { abort(); }

TEST(JUnitIntegrationTest, WaitForever) {
  while (true) my_sleep(1000000);
}

TEST(JUnitIntegrationTest, BufferOverrun) {
  int *mem = static_cast<int *>(my_malloc(127, 0));
  // Allocations are usually aligned, so even if 127 bytes were requested,
  // it's mostly safe to assume there are 128 bytes. Writing into the last
  // byte is safe for the rest of the code, but still enough to trigger
  // AddressSanitizer (ASAN) or Valgrind.
  my_atomic_store32(mem + (128 / sizeof(*mem)) - 1, 1);
  free(mem);
}

TEST(JUnitIntegrationTest, MemoryLeak) { my_malloc(127, 0); }

}  // namespace junit_integration_unittest
