disable_query_log;
#
# Check if server has support for loading plugins
#
if (`SELECT @@have_dynamic_loading != 'YES'`) {
  --skip stats_example plugin requires dynamic loading
}

#
# Check if the variable STATS_EXAMPLE is set
#
if (!$STATS_EXAMPLE) {
  --skip stats_example plugin requires the environment variable \$STATS_EXAMPLE to be set (normally done by mtr)
}

#
# Check if --plugin-dir was setup for stats_example db
#
if (`SELECT CONCAT('--plugin-dir=', REPLACE(@@plugin_dir, '\\\\', '/')) != '$STATS_EXAMPLE_OPT/'`) {
  --skip stats_example plugin requires that --plugin-dir is set to the stats_example plugin dir (either the .opt file does not contain \$STATS_EXAMPLE_OPT or another plugin is in use)
}
enable_query_log;
