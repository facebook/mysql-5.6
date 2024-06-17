$combinations = [
	['
		--no-mask
		--seed=time
		--threads=4
		--duration=400
		--queries=100M
		--reporters=QueryTimeout,Backtrace,ErrorLog,Deadlock
		--transformers=DisableOptimizations,EnableOptimizations,DisableChosenPlan,ExecuteAsDerived,DisableJoinCache,ExecuteAsPreparedThrice
		--redefine=conf/mariadb/analyze-tables-at-start.yy
		--mysqld=--log-output=FILE
		--querytimeout=30
	'], 
# NOTE: Disabled due to MDEV-6439
#		--mysqld=--slow_query_log
#		--mysqld=--long_query_time=0.000001
#		--mysqld=--log_slow_verbosity=query_plan,explain
	[
		'--validator=TransformerNoComparator --grammar=conf/mariadb/multi_update.yy --gendata=conf/mariadb/multi_update.zz',
		'--validator=TransformerLight --grammar=conf/mariadb/optimizer.yy --gendata=conf/mariadb/optimizer.zz',
		'--validator=TransformerLight --grammar=conf/mariadb/optimizer.yy',
		'--validator=TransformerLight --grammar=conf/mariadb/optimizer.yy --notnull',
		'--validator=TransformerLight --grammar=conf/mariadb/optimizer.yy --skip-gendata --mysqld=--init-file=$RQG_HOME/conf/mariadb/world.sql',
		'--validator=TransformerLight --grammar=conf/optimizer/range_access2.yy --gendata=conf/optimizer/range_access2.zz',
		'--validator=TransformerLight --grammar=conf/optimizer/range_access.yy --gendata=conf/optimizer/range_access.zz',
		'--validator=TransformerLight --grammar=conf/optimizer/outer_join.yy --gendata=conf/optimizer/outer_join.zz',
		'--validator=TransformerLight --grammar=conf/optimizer/optimizer_access_exp.yy --gendata=conf/optimizer/range_access.zz',
		'--validator=TransformerLight --grammar=conf/optimizer/optimizer_access_exp.yy --notnull',
		'--validator=TransformerLight --grammar=conf/optimizer/optimizer_access_exp.yy',
		'--validator=TransformerLight --grammar=conf/optimizer/optimizer_access_exp.yy --skip-gendata --mysqld=--init-file=$RQG_HOME/conf/mariadb/world.sql',
	], 
	[
		'--engine=MyISAM',
		'--engine=InnoDB --mysqld=--ignore-builtin-innodb --mysqld=--plugin-load=ha_innodb',
		'--engine=Aria',
		'--engine=TokuDB --mysqld=--plugin-load=ha_tokudb',
		''
	],
	[	'
			--mysqld=--use_stat_tables=PREFERABLY
			--mysqld=--optimizer_use_condition_selectivity=4 
			--mysqld=--histogram_size=100 
			--mysqld=--histogram_type=DOUBLE_PREC_HB
		',
		' --mysqld=--optimizer_selectivity_sampling_limit=100 --mysqld=--optimizer_use_condition_selectivity=5',
		''
	],
	[
		'--views=TEMPTABLE',
		'--views=MERGE'
	]
];
