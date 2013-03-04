#
# This is a simple grammar for testing the range optimizer, index_merge and sort_union
# It is based on the following principles:
#
# * No embedded perl and attempts to create balanced expressions
#
# * Limited nesting (no runaway recursive nesting), with fixed depth of 2 levels 
#
# * Using indexed columns exclusively in order to provide a lot of optimizable expressions
#
# * No joins, in order to enable larger tables without runaway queries, suitable for benchmarking and
#  avoiding situations where the optimizer would choose a full table scan due to a very small table
#
# * A smaller set of indexes in order to provide more range overlap and intersection opportunities
#
# * Both wide and narrow ranges in BETWEEN
#
# * Preference for equality expressions in order to provide ranges that actually consist of a single value
#
# * Reduced usage of NOT in order to avoid expressions that match most of the table
#
# * Use of FORCE KEY in order to prevent full table scans as much as possible
#

query_init:
	alter_add ; alter_add ; alter_add ; alter_add ; alter_add ;

query:
	alter_drop_add |
	select | select | select | select | select |
	select | select | select | select | select |
	select | select | select | select | select |
	select | select | select | select | select |
	select | select | select | select | select |
	select | select | select | select | select |
	select | select | select | select | select |
	select | select | select | select | select |
	select | select | select | select | select |
	select | select | select | select | select |
	select | select | select | select | select |
	select | select | select | select | select |
	select | select | select | select | select |
	select | select | select | select | select ;

select:
	SELECT distinct * FROM _table index_hint WHERE where order_by /* limit */ |
	SELECT distinct * FROM _table index_hint WHERE where order_by /* limit */ |
	SELECT distinct * FROM _table index_hint WHERE where order_by /* limit */ |
	SELECT distinct * FROM _table index_hint WHERE where order_by /* limit */ |
	SELECT aggregate _field_key ) FROM _table index_hint WHERE where |
	SELECT _field_key , aggregate _field_key ) FROM _table index_hint WHERE where GROUP BY _field_key ;

alter_add:
	ALTER TABLE _table ADD KEY key1 ( index_list ) ;

alter_drop_add:
	ALTER TABLE _table DROP KEY key1 ; ALTER TABLE _table[invariant] ADD KEY key1 ( index_list ) ;

distinct:
	| | DISTINCT ;

order_by:
	| | ORDER BY _field_indexed , `pk` ;

limit:
	| | | | |
	| LIMIT _digit;
	| LIMIT _tinyint_unsigned;

where:
	where_list and_or where_list ;

where_list:
	where_two and_or ( where_list ) |
	where_two and_or where_two |
	where_two and_or where_two and_or where_two |
	where_two ;

where_two:
	( temporal_item and_or temporal_item );

temporal_item:
	not ( _field_indexed comparison_operator temporal_value ) |
	_field_indexed not BETWEEN temporal_value AND temporal_value |
	_field_indexed not IN ( temporal_list ) |
	_field_indexed IS not NULL ;

aggregate:
	MIN( | MAX( | COUNT( ;

and_or:
	AND | AND | AND | AND | OR ;

or_and:
	OR | OR | OR | OR | AND ;

temporal_list:
	temporal_value , temporal_value , temporal_value |
	temporal_value , temporal_list ;

temporal_value:
	_datetime | _datetime | _timestamp | '0000-00-00 00:00:00' ;

comparison_operator:
	= | = | = | = | = | = |
	!= | > | >= | < | <= | <> ;

not:
	| | | | | | NOT ;

index_list:
	index_item , index_item |
	index_item , index_list;

index_item:
	_field ;

index_length:
	1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 ;

index_hint:
	|
	FORCE KEY ( PRIMARY , _field_indexed , _field_indexed , _field_indexed , _field_indexed );
