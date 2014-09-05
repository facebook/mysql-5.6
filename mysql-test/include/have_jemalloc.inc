# Skip test if build didn't specify -DHAVE_JEMALLOC
if (`SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.GLOBAL_STATUS WHERE VARIABLE_NAME LIKE "Jemalloc%"`)
{
  --skip Test requires jemalloc
}
