# --mysqld=--debug=d:t:i:-f,alloc_dynamic/,my_malloc/,my_multi_malloc/,mysql_select/,open_tables/,close_thread_tables/,log_general/,find_block/,make_lock_and_pin/,reg_requests/,unreg_request/,fix_paths/,translog_write_variable_record/,_ma_seq_search/:O,/dev/shm/mysqld.trace

$combinations = [
	['
		--queries=1M
		--engine=Aria
		--mysqld=--default-storage-engine=Aria
		--mysqld=--safe-mode
		--mysqld=--loose-debug-assert-if-crashed-table
		--mysqld=--loose-debug-assert-on-error
		--mysqld=--sync-sys=0
		--mysqld=--log-output=file
		--mysqld=--aria_log_purge_type=at_flush
	'],[
		'--reporters=ErrorLog,Backtrace,AriaDoubleRecovery,Shutdown',
		'--reporters=ErrorLog,Backtrace,Recovery,Shutdown'
	],[
		'--duration=30',
		'--duration=120',
		'--duration=240',
		'--duration=480'
	],[
		'--threads=1',
		'--threads=5',
		'--threads=10',
		'--threads=20'
	],[
		'--rows=1',
		'--rows=10',
		'--rows=100',
		'--rows=1000',
		'--rows=10000'
	],[
		'--mask-level=0',
		'--mask-level=1',
		'--mask-level=2'
	],[
		'',
		'--mysqld=--aria-repair-threads=2'
	],[
		'--mysqld=--loose-aria-group-commit=soft',
		'--mysqld=--loose-aria-group-commit=hard'
	],[
		'--mysqld=--loose-aria_group_commit_interval=0',
		'--mysqld=--loose-aria_group_commit_interval=1',
		'--mysqld=--loose-aria_group_commit_interval=10',
		'--mysqld=--loose-aria_group_commit_interval=100'
	],[
		'--mysqld=--aria-checkpoint-interval=0',
		'--mysqld=--aria-checkpoint-interval=1',
		'--mysqld=--aria-checkpoint-interval=120',
		'--mysqld=--aria-checkpoint-interval=32K'
	],[
		'--mysqld=--aria-block-size=1K',
		'--mysqld=--aria-block-size=2K',
		'--mysqld=--aria-block-size=4K',
		'--mysqld=--aria-block-size=8K',
		'--mysqld=--aria-block-size=16K',
		'--mysqld=--aria-block-size=32K'
	],[
		'', '',
		'--mysqld=--table_cache=32K', '--mysqld=--table_cache=10'
	],[
		'', '',
		'--mysqld=--aria-pagecache-buffer-size=512K'
	],[
		'',
		'--mysqld=--aria-pagecache-division-limit=75'
	],[
		'',
		'--mysqld=--aria_pagecache_age_threshold=10'
	],[
		'--grammar=conf/engines/engine_stress.yy --gendata=conf/engines/engine_stress.zz',
		'--grammar=conf/engines/many_indexes.yy --gendata=conf/engines/many_indexes.zz',
		'--grammar=conf/engines/tiny_inserts.yy --gendata=conf/engines/tiny_inserts.zz',
		'--grammar=conf/engines/varchar.yy --gendata=conf/engines/varchar.zz',
		'--mysqld=--init-file='.$ENV{RQG_HOME}.'/conf/smf/smf2.sql --grammar=conf/smf/smf2.yy',
		'--mysqld=--init-file='.$ENV{RQG_HOME}.'/conf/smf/smf2.sql --grammar=conf/smf/smf2.yy',
		'--mysqld=--init-file='.$ENV{RQG_HOME}.'/conf/smf/smf2.sql --grammar=conf/smf/smf2.yy',
		'--mysqld=--init-file='.$ENV{RQG_HOME}.'/conf/smf/smf2.sql --grammar=conf/smf/smf2.yy'
	]
];
