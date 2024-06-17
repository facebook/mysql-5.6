$combinations = [
	['
		--no-mask
		--seed=time
		--threads=1
		--duration=600
		--queries=100M
		--reporters=QueryTimeout,Backtrace,ErrorLog,Deadlock
		--validators=ExecutionTimeComparator
		--redefine=conf/mariadb/analyze-tables-at-start.yy
		--mysqld=--log-output=FILE
		--querytimeout=120
	'], 
	[
		'--grammar=conf/mariadb/optimizer.yy --views=TEMPTABLE',
		'--grammar=conf/mariadb/optimizer.yy --notnull --views=TEMPTABLE',
		'--grammar=conf/mariadb/optimizer.yy --views=MERGE',
		'--grammar=conf/mariadb/optimizer.yy --notnull --views=MERGE',
		'--grammar=conf/mariadb/optimizer.yy --skip-gendata --mysqld=--init-file=$RQG_HOME/conf/mariadb/world.sql',
		'--grammar=conf/optimizer/range_access2.yy --gendata=conf/optimizer/range_access2.zz',
		'--grammar=conf/optimizer/range_access.yy --gendata=conf/optimizer/range_access.zz',
		'--grammar=conf/optimizer/outer_join.yy --gendata=conf/optimizer/outer_join.zz',
		'--grammar=conf/optimizer/optimizer_access_exp.yy --views=TEMPTABLE',
		'--grammar=conf/optimizer/optimizer_access_exp.yy --notnull --views=TEMPTABLE',
		'--grammar=conf/optimizer/optimizer_access_exp.yy --views=MERGE',
		'--grammar=conf/optimizer/optimizer_access_exp.yy --notnull --views=MERGE',
		'--grammar=conf/optimizer/optimizer_access_exp.yy --skip-gendata --mysqld=--init-file=$RQG_HOME/conf/mariadb/world.sql',
	], 
	[
		'--engine=MyISAM',
		'--engine=InnoDB --mysqld=--ignore-builtin-innodb --mysqld=--plugin-load=ha_innodb',
		''
	],
	[	'
			--mysqld=--use_stat_tables=PREFERABLY
			--mysqld=--optimizer_use_condition_selectivity=3 
		'
	]
];
