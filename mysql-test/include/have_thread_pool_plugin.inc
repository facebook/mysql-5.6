disable_query_log;

--let $thread_pool_plugin_installed= `SELECT COUNT(*) = 1 FROM INFORMATION_SCHEMA.PLUGINS WHERE PLUGIN_NAME = 'THREAD_POOL'`
if (!$thread_pool_plugin_installed)
{
  --skip Use --thread-pool to run this test
}

enable_query_log;
