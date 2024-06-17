$combinations = [
	['
		--no-mask
		--seed=time
		--threads=4
		--duration=400
		--queries=100M
		--reporters=QueryTimeout,Backtrace,ErrorLog,Deadlock
		--mysqld=--log-output=FILE
		--querytimeout=30
	'], 
	[
		'--grammar=conf/mariadb/optimizer.yy --gendata=conf/mariadb/optimizer.zz',
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
		'--engine=Aria',
		'--engine=TokuDB --mysqld=--plugin-load=ha_tokudb',
		''
	],
];
