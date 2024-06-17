
$combinations = [
	[
	'
		--no-mask
		--seed=time
		--threads=32
		--duration=600
		--queries=100M
		--reporters=QueryTimeout,Backtrace,ErrorLog,Deadlock
		--redefine=conf/mariadb/general-workarounds.yy
		--redefine=conf/mariadb/10.0-features-redefine.yy
		--mysqld=--log_output=FILE
		--genconfig=conf/mariadb/10.0.cnf.template
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
		'--engine=TokuDB --mysqld=--plugin-load=ha_tokudb.so --mysqld=--loose-tokudb',
		'--engine=InnoDB --mysqld=--ignore-builtin-innodb --mysqld=--plugin-load=ha_innodb.so'
	],
];

