use Cwd;

$combinations = [
	['
		--queries=1M --duration=120 --threads=2 --seed=time
		--reporter=QueryTimeout,Deadlock,Backtrace,ErrorLog
		--validator=Transformer
		--filter=conf/optimizer/mrr.ff
		--no-mask
		--mysqld=--sql_mode=ONLY_FULL_GROUP_BY
		--mysqld=--optimizer_search_depth=6
	'],
	['', '', '', '', '',
		'--mysqld=--optimizer_switch=mrr_sort_keys=off',
		'--mysqld=--optimizer_switch=index_condition_pushdown=off',
		'--mysqld=--init-file='.getcwd().'/init/no_mrr.opt'
	],
	['', '--notnull'],
	['', '--views'],
	['','','','','', '','--valgrind-xml'],
	[
		'',
		'--engine=InnoDB',
		'--engine=MyISAM',
		'--engine=Maria'
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
		'--grammar=conf/optimizer/range_access2.yy --gendata=conf/optimizer/range_access2.zz'
	]
];
