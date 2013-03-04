#
# This combinations file is useful for running combinations.pl run against MDL and DDL changes to the server
# The options listed herein either affect locking directly, or affect the concurrency of various server operations.
# Some options also affect the usage of temporary or logging tables.
#

# Attention:
# 1. The reporter "Shutdown" is currently incompatibe with checking replication.
# 2. Tests with '--rpl_mode=statement' do not make sense because the grammar does not take into account
#    that some scenarios are unsafe when having a statement based replication. The impact would be that RQG
#    aborts the test after a short time and we have a result without value.
# 3. Comments between the alternatives lead to an PERL error

$combinations = [
	['
		--grammar=conf/runtime/WL5004_sql.yy
		--gendata=conf/runtime/WL5004_data.zz
		--queries=1M
		--duration=1200
		--reporters=Deadlock,ErrorLog,Backtrace
		--mysqld=--innodb-lock-wait-timeout=1
		--mysqld=--loose-lock-wait-timeout=1
		--mysqld=--secure-file-priv=/tmp/
	'], [
		'--engine=MyISAM',
#		'--engine=MEMORY',
		'--engine=Innodb'
	], [
		'--rows=1',
		'--rows=10',
		'--rows=100',
	],
	[
		'--threads=4',
		'--threads=8',
		'--threads=16',
		'--threads=32',
		'--threads=64'
	],
	[
		'--mysqld=--transaction-isolation=REPEATABLE-READ',
		'--mysqld=--transaction-isolation=SERIALIZABLE',
		'--mysqld=--transaction-isolation=READ-COMMITTED',
		'--mysqld=--transaction-isolation=READ-UNCOMMITTED'
	],[
		'--mysqld=--log-output=file',
		'--mysqld=--log-output=none',
		'--mysqld=--log-output=table',
		'--mysqld=--log-output=table,file'
	],[
		'--mem',
		''
	],[
		'--mysqld=--innodb_flush_log_at_trx_commit=0',
		'--mysqld=--innodb_flush_log_at_trx_commit=1'
	],[
		'--mask-level=0',
		'--mask-level=1',
		'--mask-level=2'
	],[
		'',
		'',
		'',
		'--rpl_mode=row',
		'--rpl_mode=mixed'
	],[
		'',
		'--mysqld=--big-tables'
	],[
		'',
		'--mysqld=--open_files_limit=10',
		'--mysqld=--open_files_limit=100',
		'--mysqld=--open_files_limit=1000'
	],[
		'',
		'--mysqld=--table_cache=10',
		'--mysqld=--table_cache=100',
		'--mysqld=--table_cache=1000'
	],[
		'',
		'--mysqld=--tmp_table_size=1K'
	],[
		'--mysqld=--query_cache_size=0 --mysqld=--query_cache_type=0',
		'--mysqld=--query_cache_size=1M --mysqld=--query_cache_type=1'
	]
];
