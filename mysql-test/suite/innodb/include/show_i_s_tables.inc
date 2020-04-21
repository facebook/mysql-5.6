--echo === information_schema.innodb_tables  and innodb_tablespaces ===
--disable_query_log
--replace_result  #P# #p#  #SP# #sp#

SELECT t.name 'Table Name',
	IFNULL(s.name, 'innodb_system') 'Tablespace',
	t.flag 'Table Flags',
	t.n_cols 'Columns',
	t.row_format 'Row Format',
	t.zip_page_size 'Zip Size'
	FROM information_schema.innodb_sys_tables t LEFT JOIN
	     information_schema.innodb_sys_tablespaces s
	ON t.space = s.space
	WHERE t.name not like 'mysql/%'
	AND t.name NOT LIKE 'SYS_%'
	ORDER BY t.name;
--enable_query_log

