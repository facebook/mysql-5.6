--disable_query_log ONCE
SET SESSION debug='+d,skip_dd_table_access_check';

--let $old_instant_cols = `SELECT REGEXP_SUBSTR(REGEXP_SUBSTR(se_private_data,'instant_col=[0-9]+'), '[0-9]+') FROM mysql.tables WHERE NAME like '%t1%';`

--eval $instant_add_column

--let $table_id = `SELECT id FROM mysql.tables WHERE name like '%t1%';`

--let $instant_cols = `SELECT REGEXP_SUBSTR(REGEXP_SUBSTR(se_private_data,'instant_col=[0-9]+'), '[0-9]+') FROM mysql.tables WHERE NAME like '%t1%';`

if (!$old_instant_cols)
{
  --disable_query_log
  --eval SELECT count(*) = $new_cols FROM mysql.columns WHERE table_id = $table_id AND se_private_data rlike 'default=[0-9]+|default_null=1';
  --enable_query_log
}

if ($old_instant_cols)
{

--eval SELECT $old_instant_cols = REGEXP_SUBSTR(REGEXP_SUBSTR(se_private_data,'instant_col=[0-9]+'), '[0-9]+') AS `Instant columns equal` FROM mysql.tables WHERE name like '%t1%'
}

--disable_query_log ONCE
SET SESSION debug= '-d,skip_dd_table_access_check';
CHECK TABLE t1;
