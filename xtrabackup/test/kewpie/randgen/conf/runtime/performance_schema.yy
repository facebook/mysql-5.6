# Copyright (C) 2010 Sun Microsystems, Inc. All rights reserved.
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

thread1:
	ddl ;			# Issue DDL that are reflected in INFORMATION_SCHEMA and cause PERFORMANCE_SCHEMA events


query:
	dml |
	{ @nonaggregates = () ; @table_names = () ; @database_names = () ; $tables = 0 ; $fields = 0 ; "" } select |
	update_settings |
	truncate |
	show_engine ;

yes_no:
	'YES' | 'NO' ;

enabled_timed:
	ENABLED | TIMED ;

update_settings:
	update_consumers |
	update_instruments |
	update_timers ;

update_consumers:
	UPDATE performance_schema . setup_consumers SET enabled = yes_no WHERE name IN ( consumer_list ) |
	UPDATE performance_schema . setup_consumers SET enabled = yes_no WHERE name LIKE consumer_category ;

update_instruments:
	UPDATE performance_schema . setup_instruments SET enabled_timed = yes_no WHERE NAME LIKE instrument_category |
	UPDATE performance_schema . setup_instruments SET enabled_timed = yes_no ORDER BY RAND() LIMIT _digit ;

update_timers:
	UPDATE performance_schema . setup_timers SET timer_name = timer_type ;

truncate:
	TRUNCATE TABLE performance_schema . truncateable_table ;

truncateable_table:
	events_waits_current |
	events_waits_history | events_waits_history_long |
	events_waits_summary_by_event_name | events_waits_summary_by_instance | events_waits_summary_by_thread_by_event_name |
	file_summary_by_event_name | file_summary_by_instance ;

consumer_list:
	consumer | 
	consumer_list , consumer ;

consumer:
	'events_waits_current' |
	'events_waits_history' |
	'events_waits_history_long' |
	'events_waits_summary_by_thread_by_event_name' |
	'events_waits_summary_by_event_name' |
	'events_waits_summary_by_instance' |
	'file_summary_by_event_name' |
	'file_summary_by_instance';

consumer_category:
	'events%' | 'file%';

instrument_category:
	'wait%' |
	'wait/synch%' | 'wait/io%' |
	'wait/synch/mutex/%' | 'wait/synch/rwlock%' | 'wait/synch/cond%' |
	'%mysys%' | '%sql%' | '%myisam%' ;

timer_type:
	'CYCLE' | 'NANOSECOND' | 'MICROSECOND' | 'MILLISECOND' | 'TICK' ;

show_engine:
	SHOW ENGINE PERFORMANCE_SCHEMA STATUS ;

ddl:
#	character_sets |
#	collations |
#	collation_character_set_applicability |
	columns |
	column_privileges |
#	engines |
	events |
#	files |
#	global_status |
#	global_variables |
	key_column_usage |
	parameters |
	partitions |
#	plugins |
#	processlist |
#	profiling |
#	referential_constraints |
#	routines |		# same as parameters
	schemata |
	schema_privileges |
#	session_status |
#	session_variables |
#	statistics |
	tables |
#	tablespaces |
	table_constraints |
	table_privileges |
	triggers |
	user_privileges |
	views ;

columns:
	ALTER TABLE _table ADD COLUMN _letter INTEGER DEFAULT NULL |
	ALTER TABLE _table DROP COLUMN _letter ;

column_privileges:
	GRANT privilege_list ON _table TO 'someuser'@'somehost';

events:
	CREATE EVENT _letter ON SCHEDULE AT NOW() DO SET @a=@a |
	DROP EVENT _letter ;

key_column_usage:
	ALTER TABLE _table ADD KEY ( _letter ) |
	ALTER TABLE _table DROP KEY _letter ;

parameters:
	CREATE PROCEDURE _letter ( procedure_parameter_list ) BEGIN SELECT COUNT(*) INTO @a FROM _table; END ; |
	DROP PROCEDURE IF EXISTS _letter |
	CREATE FUNCTION _letter ( function_parameter_list ) RETURNS INTEGER RETURN 1 |
	DROP FUNCTION IF EXISTS _letter ;

partitions:
	ALTER TABLE _table PARTITION BY KEY() PARTITIONS _digit |
	ALTER TABLE _table REMOVE PARTITIONING ;

schemata:
	CREATE DATABASE IF NOT EXISTS _letter |
	DROP DATABASE IF EXISTS _letter ;

schema_privileges:
	GRANT ALL PRIVILEGES ON _letter . * TO 'someuser'@'somehost' |
	REVOKE ALL PRIVILEGES ON _letter . * FROM 'someuser'@'somehost' ; 

tables:
	CREATE TABLE _letter LIKE _table |
	DROP TABLE _letter ;

table_constraints:
	ALTER TABLE _table DROP PRIMARY KEY |
	ALTER TABLE _table ADD PRIMARY KEY (`pk`) ;

table_privileges:
	GRANT ALL PRIVILEGES ON test . _letter TO 'someuser'@'somehost' |
	REVOKE ALL PRIVILEGES ON test . _letter FROM 'someuser'@'somehost' ;

triggers:
	CREATE TRIGGER _letter BEFORE INSERT ON _table FOR EACH ROW BEGIN INSERT INTO _table SELECT * FROM _table LIMIT 0 ; END ; |
	DROP TRIGGER IF EXISTS _letter;

user_privileges:
	GRANT admin_privilege_list ON * . * to 'someuser'@'somehost' |
	REVOKE admin_privilege_list ON * . * FROM 'someuser'@'somehost' ;

admin_privilege_list:
	admin_privilege |
	admin_privilege , admin_privilege_list ;

admin_privilege:
	CREATE USER |
	PROCESS |
	RELOAD |
	REPLICATION CLIENT |
	REPLICATION SLAVE |
	SHOW DATABASES |
	SHUTDOWN |
	SUPER |
#	ALL PRIVILEGES |
	USAGE ;

views:
	CREATE OR REPLACE VIEW _letter AS SELECT * FROM _table |
	DROP VIEW IF EXISTS _letter ;

function_parameter_list:
	_letter INTEGER , _letter INTEGER ;

procedure_parameter_list:
	parameter |
	parameter , procedure_parameter_list ;

parameter:
	in_out _letter INT ;

in_out:
	IN | OUT ;
	

privilege_list:
	privilege_item |
	privilege_item , privilege_list ;

privilege_item:
	privilege ( field_list );

privilege:
	INSERT | SELECT | UPDATE ;

field_list:
	_field |
	_field , field_list ;



select:
	SELECT *
	FROM join_list
	where
	group_by
	having
	order_by_limit
;

select_list:
	new_select_item |
	new_select_item , select_list ;

join_list:
	new_table_item |
	new_table_item |
	new_table_item |
	new_table_item |
	(new_table_item join_type new_table_item ON ( current_table_item . _field = previous_table_item . _field ) ) ;

join_type:
	INNER JOIN | left_right outer JOIN | STRAIGHT_JOIN ;  

left_right:
	LEFT | RIGHT ;

outer:
	| OUTER ;
where:
	|
	WHERE where_list ;

where_list:
	not where_item |
	not (where_list AND where_item) |
	not (where_list OR where_item) ;

not:
	| | | NOT;

where_item:
	existing_table_item . _field IN ( _digit , _digit , _digit ) |
	existing_table_item . _field LIKE instrument_category |
	existing_table_item . _field sign value |
	existing_table_item . _field sign existing_table_item . _field ;

group_by:
	{ scalar(@nonaggregates) > 0 ? " GROUP BY ".join (', ' , @nonaggregates ) : "" };

having:
	| HAVING having_list;

having_list:
	not having_item |
	not (having_list AND having_item) |
	not (having_list OR having_item) |
	having_item IS not NULL ;

having_item:
	existing_table_item . _field sign value ;

order_by_limit:
	LIMIT _tinyint_unsigned	|
#	ORDER BY order_by_list |
	ORDER BY order_by_list LIMIT _tinyint_unsigned ;

total_order_by:
	{ join(', ', map { "field".$_ } (1..$fields) ) };

order_by_list:
	order_by_item |
	order_by_item , order_by_list ;

order_by_item:
	existing_table_item . _field ;

limit:
	| LIMIT _digit | LIMIT _digit OFFSET _digit;

new_select_item:
	nonaggregate_select_item |
	nonaggregate_select_item |
	aggregate_select_item;

nonaggregate_select_item:
	table_one_two . _field AS { my $f = "field".++$fields ; push @nonaggregates , $f ; $f} ;

aggregate_select_item:
	aggregate table_one_two . _field ) AS { "field".++$fields };

# Only 20% table2, since sometimes table2 is not present at all

table_one_two:
	table1 { $last_table = $tables[1] } | 
	table2 { $last_table = $tables[2] } ;

aggregate:
	COUNT( | SUM( | MIN( | MAX( ;

new_table_item:
	database . _table AS { $database_names[++$tables] = $last_database ; $table_names[$tables] = $last_table ; "table".$tables };

database:
	{ $last_database = $prng->arrayElement(['mysql','test','INFORMATION_SCHEMA','performance_schema']); return $last_database };

current_table_item:
	{ $last_database = $database_names[$tables] ; $last_table = $table_names[$tables] ; "table".$tables };

previous_table_item:
	{ $last_database = $database_names[$tables-1] ; $last_table = $table_names[$tables-1] ; "table".($tables - 1) };

existing_table_item:
	{ my $i = $prng->int(1,$tables) ; $last_database = $database_names[$i]; $last_table = $table_names[$i] ; "table".$i };

existing_select_item:
	{ "field".$prng->int(1,$fields) };

sign:
	= | > | < | != | <> | <= | >= ;
	
value:
	_digit | _char(2) | _datetime ;

dml:
        update | insert | delete ;

update:
        UPDATE _table SET _field = value WHERE _field sign value ;

delete:
        DELETE FROM _table WHERE _field sign value LIMIT _digit ;

insert:
        INSERT INTO _table ( `pk` ) VALUES  (NULL);

