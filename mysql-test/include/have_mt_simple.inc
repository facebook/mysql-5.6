#
# Check if server has support for loading plugins
#
if (`SELECT @@have_dynamic_loading != 'YES'`) {
  --skip simple parser requires dynamic loading
}

#
# Check if the variable MT_SIMPLE is set
#
if (!$MT_SIMPLE) {
  --skip mt_simple requires the environment variable \$MT_SIMPLE to be set (normally done by mtr)
}

#
# Check if --plugin-dir was setup for simple parser
#
if (`SELECT CONCAT('--plugin-dir=', REPLACE(@@plugin_dir, '\\\\', '/')) != '$MT_SIMPLE_OPT/'`) {
  --skip mt_simple requires that --plugin-dir is set to the udf plugin dir (either the .opt file does not contain \$UDF_EXAMPLE_LIB_OPT or another plugin is in use)
}

