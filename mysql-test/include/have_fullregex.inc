# Skip test if we didn't compile with a compiler that supports full regex
# Currently only gcc is tested (gcc 4.9 is the first version to support full
# regex).  All other compilers are assumed to support it.
# Add further checks as we get information on these compilers

--disable_query_log
set @comp_version = @@version_compile_compiler_major * 100 +
                    @@version_compile_compiler_minor;
set @is_gnu = STRCMP(@@version_compile_compiler, "GNU") = 0;
--enable_query_log
if (`SELECT IF (@is_gnu, IF (@comp_version > 408, 1, 0), 1) = 0`)
{
  --skip Test requires full regex (not supported in gcc 4.8 and prior)
}
