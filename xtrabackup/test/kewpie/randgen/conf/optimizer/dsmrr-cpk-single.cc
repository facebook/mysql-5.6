use Cwd;
	
$combinations = [
	['
		--queries=1M
		--duration=300
		--threads=2
		--seed=time
		--reporter=QueryTimeout,Deadlock,Backtrace,ErrorLog
		--validator=Transformer
		--filter=conf/optimizer/dsmrr-cpk.ff
                --no-mask
                --mysqld=--sql_mode=ONLY_FULL_GROUP_BY
                --mysqld=--optimizer_search_depth=6
		--debug
        '],
        ['', '', '', '', '',
                '--mysqld=--optimizer_switch=mrr_sort_keys=off',
                '--mysqld=--optimizer_switch=index_condition_pushdown=off',
        ],
	['', '--notnull'],
	['', '--views'],
	['', '', '', '', '--valgrind-xml'],
	[
		'--engine=InnoDB', '--engine=InnoDB', '--engine=InnoDB',
		'--engine=InnoDB', '--engine=InnoDB', '--engine=InnoDB',
		'--engine=InnoDB', '--engine=InnoDB', '--engine=InnoDB',
		'--engine=MyISAM', '--engine=Maria', '--engine=PBXT'
	],[
		'',
		'--mysqld=--join_buffer_size=1',
		'--mysqld=--join_buffer_size=100',
		'--mysqld=--join_buffer_size=1K',
		'--mysqld=--join_buffer_size=10K',
		'--mysqld=--join_buffer_size=100K'
	],[
                '--mysqld=--join_cache_level=0',
                '--mysqld=--join_cache_level=1',
                '--mysqld=--join_cache_level=2',
                '--mysqld=--join_cache_level=3',
                '--mysqld=--join_cache_level=4',
                '--mysqld=--join_cache_level=5',
                '--mysqld=--join_cache_level=6',
                '--mysqld=--join_cache_level=7',
                '--mysqld=--join_cache_level=8'
        ],[
                '',
                '--mysqld=--join_buffer_size=1',
                '--mysqld=--join_buffer_size=100',
                '--mysqld=--join_buffer_size=1K',
                '--mysqld=--join_buffer_size=10K',
                '--mysqld=--join_buffer_size=100K'
        ],[
                '',
                '--mysqld=--mrr_buffer_size=1',
                '--mysqld=--mrr_buffer_size=100',
                '--mysqld=--mrr_buffer_size=1K',
                '--mysqld=--mrr_buffer_size=10K',
                '--mysqld=--mrr_buffer_size=100K'
	],[
		'--grammar=conf/optimizer/optimizer_no_subquery.yy',
		'--grammar=conf/optimizer/outer_join.yy --gendata=conf/optimizer/outer_join.zz',
		'--grammar=conf/optimizer/range_access.yy --gendata=conf/optimizer/range_access.zz',
		'--grammar=conf/dbt3/dbt3-joins.yy --mysqld=--init-file='.getcwd().'/conf/dbt3/dbt3-s0.001.dump'
	]
];
