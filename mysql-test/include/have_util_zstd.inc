# ==== Purpose ====
#
# Check if zstd utility is available
#
# ==== Usage ====
#
# --source include/have_util_zstd.inc
# ...
# --exec zstd some options
#

disable_result_log;
disable_query_log;

--let $zstd=
--let $_outfile= $MYSQLTEST_VARDIR/tmp/have_zstd.tmp

# find zstd utility

--error 0,1,127
--exec zstd -h > $_outfile 2>&1
--eval SET @content = LOAD_FILE('$_outfile')
--remove_file $_outfile

if (`SELECT LOCATE('Usage', @content) > 0`) {
  --let $zstd=zstd
  SET @content="";
}

# Skip the test case if utility not found
if (`SELECT '$zstd' = ''`) {
  --skip the test zstd utility not found
}

enable_query_log;
enable_result_log;

