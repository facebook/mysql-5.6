use Cwd;

$combinations = [
	['
		--vardir2=/tmp/vardir2
		--queries=1M
		--duration=300
		--threads=2
		--reporter=QueryTimeout,Backtrace,ErrorLog,Deadlock
		--validator=ResultsetComparatorSimplify
		--filter=conf/optimizer/dsmrr-cpk.ff
	'],
	['--notnull', ''],
	['--views', ''],
	[
		'--engine=InnoDB', '--engine=InnoDB', '--engine=InnoDB',
		'--engine=InnoDB', '--engine=InnoDB', '--engine=InnoDB', 
		'--engine=MyISAM', '--engine=Maria', '--engine=PBXT',
	],[
		'--mysqld1=--join_cache_level=0 --mysqld2=--join_cache_level=0',
		'--mysqld1=--join_cache_level=1 --mysqld2=--join_cache_level=1',
		'--mysqld1=--join_cache_level=2 --mysqld2=--join_cache_level=2',
		'--mysqld1=--join_cache_level=3 --mysqld2=--join_cache_level=3',
		'--mysqld1=--join_cache_level=4 --mysqld2=--join_cache_level=4',
		'--mysqld1=--join_cache_level=5 --mysqld2=--join_cache_level=5',
		'--mysqld1=--join_cache_level=6 --mysqld2=--join_cache_level=6',
		'--mysqld1=--join_cache_level=7 --mysqld2=--join_cache_level=7',
		'--mysqld1=--join_cache_level=8 --mysqld2=--join_cache_level=8'
	],[
		'',
		'--mysqld1=--join_buffer_size=1 --mysqld2=--join_buffer_size=1',
		'--mysqld1=--join_buffer_size=100 --mysqld2=--join_buffer_size=100',
		'--mysqld1=--join_buffer_size=1K --mysqld2=--join_buffer_size=1K',
		'--mysqld1=--join_buffer_size=10K --mysqld2=--join_buffer_size=10K',
		'--mysqld1=--join_buffer_size=100K --mysqld2=--join_buffer_size=100K'
	],[
		'
			--basedir2=/home/philips/bzr/maria-5.3
		',
		'
			--mysqld1=--optimizer_switch=mrr_sort_keys=on
			--mysqld2=--optimizer_switch=mrr_sort_keys=off
		'
	],[
		'--grammar=conf/optimizer/optimizer_no_subquery.yy',
		'--grammar=conf/optimizer/outer_join.yy --gendata=conf/optimizer/outer_join.zz',
		'--grammar=conf/optimizer/range_access.yy --gendata=conf/optimizer/range_access.zz',
		'--grammar=conf/dbt3/dbt3-joins.yy
		 --mysqld1=--init-file='.getcwd().'/conf/dbt3/dbt3-s0.001.dump
		 --mysqld2=--init-file='.getcwd().'/conf/dbt3/dbt3-s0.001.dump'
	]
];
