# Skip test if build didn't specify -DHAVE_JEMALLOC
if (`SELECT COUNT(*) = 0 FROM performance_schema.global_status WHERE VARIABLE_NAME LIKE "Jemalloc%"`)
{
  --skip Test requires jemalloc
}
