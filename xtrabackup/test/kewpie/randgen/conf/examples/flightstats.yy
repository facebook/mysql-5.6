# Copyright (C) 2008-2009 Sun Microsystems, Inc. All rights reserved.
# Use is subject to license terms.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301
# USA

#
# This grammar is an example on how to create grammars that operate against tables
# which have different structure and it only makes sense to join them in certain
# ways and on certain fields. The following principles apply:
#
# * The first table is always the ontime table, with alias a1. This provies a fixed reference
#	point for subsequent joins
#
# * The SELECT list is either SELECT * , SELECT COUNT(*) , or may use the fact that the fields
#	in the a1 table are always known
#
# * There is always a join, therefore there is always an a2 table, however it may be one of
#	several options
#
# * Each potential join is listed separately with a specific join condition that should be realistic
# 
# * In the WHERE clause, if we generate a condition for which the table is not present, the condition is
#	commented out in order to avoid semantic errors
#

query_init:
	USE flightstats;

# The queries from this grammar may produce resultsets of varying length
# to avoid excessive memory usage, we reduce all queries to a COUNT(*)
# This way, we also do not have to tinker with any field names in the SELECT list

query:
	{ $alias_count = 0 ; %tables = () ; %aliases = () ; return undef ; }
	select ;

select:
	SELECT aggregate_item FROM join_list WHERE where |
	SELECT a1 . * FROM join_list WHERE where ORDER BY a1 . `id` LIMIT _digit ;

aggregate_item:
	COUNT(*) | MIN(a1 . dep_time) | SUM( distinct a1 . distance) | MAX(a1 . id) | COUNT( distinct a1 . tail_num ) ;

distinct:
	| DISTINCT ;

#
# We divide the joins into "big", those containing the `ontime` table, and 
# small, those containing stuff like states and ZIP codes.
#

join_list:
#	big_join_item |
	( big_join_item ) third_join_item |
	( big_join_item ) CROSS JOIN small_join_item ;

big_join_item:
	ontime2carrier | ontime2airport | ontime2aircraft ;
	
small_join_item:
	airport2state | airport2zipcode | aircraft2engine | airport2remark;

third_join_item:
	first2carrier | first2airport | first2aircraft ;

#
# Here we define only joins that are meaningful, useful and very likely to use indexes.
#

ontime2carrier:
	ontime_table INNER JOIN carrier_table ON ( previous_table . `carrier` = current_table . `code` );

first2carrier:
	INNER JOIN carrier_table ON ( a1 . `carrier` = current_table . `code` );

ontime2airport:
	ontime_table INNER JOIN airport_table ON ( previous_table . origin_destination = current_table .`code` ) ;

first2airport:
	INNER JOIN airport_table ON ( a1 . origin_destination = current_table .`code` );
	
origin_destination:
	`origin` | `destination` ;

ontime2aircraft:
	ontime_table INNER JOIN aircraft_table ON ( current_table .`tail_num` = previous_table .`tail_num` ) ;

first2aircraft:
	INNER JOIN aircraft_table ON ( a1 .`tail_num` = current_table .`tail_num` );

airport2state:
	airport_table INNER JOIN state_table ON ( previous_table . `state` = current_table . `state_code` );

airport2zipcode:
	airport_table INNER JOIN zipcode_table ON ( previous_table . `state` = current_table . `state_code` );

airport2remark:
	airport_table INNER JOIN remark_table USING ( `site_number` ) ;

aircraft2engine:
	aircraft_table INNER JOIN engine_table USING ( `aircraft_engine_code` ) ;

ontime_table:
	`ontime_mysiam` { $table_name = 'ontime'; return undef; } new_table;

carrier_table:
	`carriers` { $table_name = 'carriers'; return undef; } new_table ;

airport_table:
	`airports` { $table_name = 'airports'; return undef; } new_table ;

remark_table:
	`airport_remarks` { $table_name = 'airport_remarks' ; return undef; } new_table ;

aircraft_table:
	`aircraft` { $table_name = 'aircraft'; return undef; } new_table;

engine_table:
	`aircraft_engines` { $table_name = 'aircraft_engines' ; return undef ; } new_table;

state_table:
	`states` { $table_name = 'states' ; return undef; } new_table ;

zipcode_table:
	`zipcodes` { $table_name = 'zipcodes' ; return undef; } new_table ;

#
# We always have a WHERE and it contains a lot of expressions combined with an AND in order to provide
# numerous opportunities for optimization and reduce calculation times.
# In addition, we always define at least one condition against the `ontime` table.
#

where:
	{ $condition_table = 'ontime' ; return undef; } start_condition ontime_condition end_condition AND
	( where_list );

where_list:
	where_condition AND where_condition AND where_condition AND where_condition AND where_condition AND where_condition ;

#
# Each of the conditions described below are valid and meaningful for the particular table in question
# They are likely to use indexes and/or zero down on a smaller number of records
# 

where_condition:
	{ $condition_table = 'ontime' ; return undef; } start_condition ontime_condition end_condition |
	{ $condition_table = 'carriers' ; return undef; } start_condition carrier_condition end_condition |
	{ $condition_table = 'aircraft'; return undef; } start_condition aircraft_condition end_condition |
	{ $condition_table = 'airports'; return undef; } start_condition airport_condition end_condition |
	{ $condition_table = 'states'; return undef; } start_condition state_condition end_condition |
	{ $condition_table = 'zipcodes'; return undef; } start_condition zipcode_condition end_condition |
	{ $condition_table = 'airport_remarks'; return undef; } start_condition remark_condition end_condition |
	{ $condition_table = 'aircraft_engines'; return undef; } start_condition engine_condition end_condition ;

ontime_condition:
	table_alias . `carrier` generic_carrier_expression |
	table_alias . `origin` generic_code_expression |
	table_alias . `destination` generic_code_expression |
	table_alias . `origin` generic_code_expression AND table_alias . `destination` generic_code_expression |
	table_alias . `origin` generic_code_expression OR table_alias . `destination` generic_code_expression |
	table_alias . `tail_num` generic_char_expression ;

state_condition:
	table_alias . `state_code` generic_state_expression |
	table_alias . `name` generic_char_expression ;

zipcode_condition:
	table_alias . `zipcode` BETWEEN 10000 + ( _tinyint_unsigned * 100) AND 10000 + ( _tinyint_unsigned * 100) ;

table_alias:
	{ my $alias = shift @{$aliases{$condition_table}}; push @{$aliases{$condition_table}} , $alias ; return $alias } ;

carrier_condition:
	table_alias . `code` generic_carrier_expression;

generic_carrier_expression:
	= single_carrier |
	IN ( carrier_list ) ;

airport_condition:
	table_alias . `code` generic_code_expression |
	table_alias . `state` generic_state_expression |
	( table_alias . `state` generic_state_expression ) AND ( table_alias . `city` generic_char_expression) |
	table_alias . `longitude` BETWEEN _tinyint AND _tinyint_unsigned ;

aircraft_condition:
	table_alias . `tail_num` generic_char_expression |
	table_alias . `state` generic_state_expression ;

engine_condition:
	table_alias . `manufacturer` generic_char_expression ;

remark_condition:
	table_alias . `airport_remark_id` BETWEEN _tinyint_unsigned AND _smallint_unsigned ;

generic_char_expression:
	BETWEEN _char[invariant] AND CHAR(ASCII( _char[invariant] ) + one_two ) |
	LIKE 'N10%' |	# 6098 aircraft with tail num starting with N10 
	LIKE 'N9Q%' ;	# 10 aircraft starting with N9Q

one_two:
	1 | 2 ;

generic_code_expression:
#	BETWEEN _char[invariant] AND CHAR(ASCII( _char[invariant] ) + one_two ) |
	= single_airport |
	IN ( airport_list ) ;

single_airport:
	'ORD' |		# busiest airport
	'AKN' |		# un-busiest airport
	'BIS' |		# 100 flights 
	'LIT' |		# 1000 flights
	'MSP' ; 	# 10000 flights

airport_list:
	single_airport |
	single_airport , airport_list ;

generic_state_expression:
	= single_state |
	IN ( state_list ) |
	BETWEEN _char(2) AND _char(2) ;

state_list:
	single_state |
	single_state , state_list ;

carrier_list:
	single_carrier |
	single_carrier , carrier_list ;

single_state:
	'AK' | 'AL' | 'AR' | 'AS' | 'AZ' | 'CA' | 'CO' | 'CQ' | 'CT' | 'DC' | 'DE' | 'FL' | 'GA' | 'GU' | 'HI' | 'IA' | 'ID' | 'IL' | 'IN' | 'KS' | 'KY' | 'LA' | 'MA' | 'MD' | 'ME' | 'MI' | 'MN' | 'MO' | 'MQ' | 'MS' | 'MT' | 'NC' | 'ND' | 'NE' | 'NH' | 'NJ' | 'NM' | 'NV' | 'NY' | 'OH' | 'OK' | 'OR' | 'PA' | 'PR' | 'RI' | 'SC' | 'SD' | 'TN' | 'TX' | 'UT' | 'VA' | 'VI' | 'VT' | 'WA' | 'WI' | 'WQ' | 'WV' | 'WY' ;

single_carrier:
	'AA'|'AQ'|'AS'|'B6'|'CO'|'DH'|'DL'|'EV'|'FL'|'HA'|'HP'|'MQ'|'NW'|'OH'|'OO'|'RU'|'TW'|'TZ'|'UA'|'US'|'WN';

#
# When we define a condition, we check if the table for which this condition would apply is present in 
# the list of the tables we selected for joining. If the table is not present, the condition is still
# generated, but it is commented out in order to avoid "unknown table" errors.
#

start_condition:
	{ ((exists $tables{$condition_table}) ? '' : '/* ') } ;

end_condition:
	{ ((exists $tables{$condition_table}) ? '' : '*/ 1 = 1 ') };

new_table:
	AS { $alias_count++ ; $tables{$table_name}++ ; push @{$aliases{$table_name}}, 'a'.$alias_count ; return 'a'.$alias_count }  ;

current_table:
	{ 'a'.$alias_count };

previous_table:
	{ 'a'.($alias_count - 1) };
