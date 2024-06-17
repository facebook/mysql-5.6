
$combinations = [
	[
	'
		--no-mask
		--seed=time
		--threads=8
		--duration=600
		--queries=100M
		--reporters=QueryTimeout,Backtrace,ErrorLog,Deadlock
		--validators=TransformerNoComparator
		--transformers=ExecuteAsPreparedTwice,DisableOptimizations,EnableOptimizations,OrderBy
		--redefine=conf/mariadb/general-workarounds.yy
		--mysqld=--log_output=FILE
		--mysqld=--log_bin_trust_function_creators=1
		--mysqld=--query_cache_size=64M
	'], 
	[
		'--views --grammar=conf/runtime/metadata_stability.yy --gendata=conf/runtime/metadata_stability.zz',
		'--views --grammar=conf/runtime/performance_schema.yy',
		'--views --grammar=conf/runtime/information_schema.yy',
		'--views --grammar=conf/engines/many_indexes.yy --gendata=conf/engines/many_indexes.zz',
		'--grammar=conf/engines/engine_stress.yy --gendata=conf/engines/engine_stress.zz',
		'--views --grammar=conf/partitioning/partitions.yy',
		'--views --grammar=conf/partitioning/partition_pruning.yy --gendata=conf/partitioning/partition_pruning.zz',
		'--views --grammar=conf/replication/replication.yy --gendata=conf/replication/replication-5.1.zz',
		'--grammar=conf/replication/replication-ddl_sql.yy --gendata=conf/replication/replication-ddl_data.zz',
		'--views --grammar=conf/replication/replication-dml_sql.yy --gendata=conf/replication/replication-dml_data.zz',
		'--views --grammar=conf/runtime/connect_kill_sql.yy --gendata=conf/runtime/connect_kill_data.zz',
		'--views --grammar=conf/runtime/WL5004_sql.yy --gendata=conf/runtime/WL5004_data.zz'
	],
	[
		'--engine=InnoDB',
		'--engine=MyISAM',
		'--engine=Aria',
		'',
		'--engine=InnoDB --mysqld=--ignore-builtin-innodb --mysqld=--plugin-load=ha_innodb.so'
	],
# slave-skip-errors: 
# 1054: MySQL:67878 (LOAD DATA in views)
# 1317: Query partially completed on the master (MDEV-368 which won't be fixed)
# 1049, 1305, 1539: MySQL:65428 (Unknown database) - fixed in 5.7.0
# 1505: MySQL:64041 (Partition management on a not partitioned table) 
	[
		'--rpl_mode=row --mysqld=--slave-skip-errors=1049,1305,1539,1505',
		'--rpl_mode=mixed --mysqld=--slave-skip-errors=1049,1305,1539,1505,1317',
	]
];

