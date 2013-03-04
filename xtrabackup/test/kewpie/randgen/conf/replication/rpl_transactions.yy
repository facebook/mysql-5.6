# 
# The purpose of this grammar is to be a general-purpose tests for transactional replication
# and the related features, such as group commit, point-in-time snapshots, innodb early lock release, etc.
#
# Design considerations:
# * We have mostly non-conflicting queries in order to enable group commit
# * Simple grammar rules without too much embedded perl code
# * Some SBR corner cases which are difficult to get right are not included
# * FLUSH LOGS is made deliberately rare to avoid dozens of log rotations per minute
#


query:
	99pct_chance_query | 99pct_chance_query | 99pct_chance_query | 99pct_chance_query | 99pct_chance_query |
	99pct_chance_query | 99pct_chance_query | 99pct_chance_query | 99pct_chance_query | 10pct_chance_query ;

10pct_chance_query:
	99pct_chance_query | 99pct_chance_query | 99pct_chance_query | 99pct_chance_query | 99pct_chance_query |
	99pct_chance_query | 99pct_chance_query | 99pct_chance_query | 99pct_chance_query | 1pct_chance_query ;

1pct_chance_query:
	USE test_test1 |
	99pct_chance_query | 99pct_chance_query | 99pct_chance_query | 99pct_chance_query | 99pct_chance_query |
	99pct_chance_query | 99pct_chance_query | 99pct_chance_query | 99pct_chance_query | 01pct_chance_query ;

99pct_chance_query:
	fully_formed_transaction | standalone_query ;

test_test1:
	test | test1 ;

01pct_chance_query:
	FLUSH LOGS ;

fully_formed_transaction:
	SET AUTOCOMMIT=OFF ; START TRANSACTION ; transaction_body ; commit_rollback ;

transaction_body:
	standalone_list |
	standalone_list ; SAVEPOINT s1 ; standalone_list ; rollback_release_savepoint ; standalone_list ;

rollback_release_savepoint:
	| 
	RELEASE SAVEPOINT s1 |
	ROLLBACK TO SAVEPOINT s1 ;

standalone_list:
	standalone_query ; standalone_query ;
	standalone_query ; standalone_list ;

commit_rollback:
	COMMIT | COMMIT | COMMIT | COMMIT | COMMIT |
	COMMIT | COMMIT | COMMIT | COMMIT | ROLLBACK ;

# In this grammar, we make a distinction between rules that are included in order to elicit specific binlog
# events and rules that are included in order to simulate a more realistic workload

standalone_query:
	/* QUERY_IS_REPLICATION_SAFE */ sql_statement ;
#	binlog_event | sql_statement ;	# TODO

sql_statement:
	insert | insert | insert | insert | insert |
	update | update | update | update |
	delete ;

insert:
	insert_single | insert_multi ;

#| insert_select ;		# TODO , not SBR safe

insert_single:
	INSERT ignore INTO _table ( _field_no_pk ) VALUES ( value ) ;

insert_multi:
	INSERT ignore INTO _table ( _field_no_pk , _field_next , _field_next ) VALUES row_list ;

insert_select:
	INSERT ignore INTO _table ( _field_no_pk , _field_no_pk ) SELECT _field , _field FROM _table where_optional ORDER BY _field_list LIMIT _digit ;

ignore:
	| IGNORE | IGNORE | IGNORE ;

update:
	update_single ;

update_single:
	UPDATE ignore _table SET update_list where_optional order_by ;

update_list:
	update_item , update_item | update_item , update_list ;

update_item:
	_field_next = value | _field_nokey = value | _field_nokey = value ;

delete:
	DELETE FROM _table where_mandatory order_by ;

# Even if WHERE is "optional", make most queries include one to avoid excessive transaction conflicts

where_optional:
	| where_mandatory | where_mandatory | where_mandatory | where_mandatory | where_mandatory ;

where_mandatory:
	WHERE `pk` = value |
	WHERE _field_key = value |
	WHERE _field_key IN ( value_list ) ;

order_by:
	| | |
	ORDER BY _field |
	ORDER BY field_set |
	ORDER BY _field_list ;

field_set:
	_field , _field |
	_field , field_set ;
	
row_list:
	row , row | row_list , row ;

row:
	( value , value , value ) ;

value_list:
	value , value | value , value_list ;

value:
	nonconflicting_value | nonconflicting_value | nonconflicting_value |  nonconflicting_value | nonconflicting_value |
	nonconflicting_value | nonconflicting_value | nonconflicting_value |  nonconflicting_value | conflicting_value ;

nonconflicting_value:
	{ $$ } | REPEAT(LPAD(CONNECTION_ID(), 2, ' '), _digit ) | CONNECTION_ID() ;

conflicting_value:
	_tinyint_unsigned | _digit | _varchar(1) | _varchar(64) | { time() } ;
