use Cwd;

$combinations = [
	['
		--queries=10 --duration=120 --threads=2 --seed=time
		--reporter=QueryTimeout,Deadlock,Backtrace,ErrorLog
		--gendata
		--filter=conf/optimizer/mrr.ff
		--no-mask
		--mysqld=--sql_mode=ONLY_FULL_GROUP_BY
		--mysqld=--optimizer_search_depth=6
	'],
	['',
#		'--mysqld=--optimizer_switch=index_condition_pushdown=off',
		'--mysqld=--init-file='.getcwd().'/init/no_mrr.opt'
	],
	['', '--notnull'],
	[
		'--engine=InnoDB',
		'--engine=MyISAM',
	],[
		'',
		'--mysqld=--join_buffer_size=1',
		'--mysqld=--join_buffer_size=100',
	],[
		'--grammar=conf/optimizer/optimizer_no_subquery.yy --views',
		'--grammar=conf/optimizer/outer_join.yy --gendata=conf/optimizer/outer_join.zz',
		'--grammar=conf/optimizer/range_access.yy --gendata=conf/optimizer/range_access.zz',
		'--grammar=conf/optimizer/range_access2.yy --gendata=conf/optimizer/range_access2.zz'
	]
];
