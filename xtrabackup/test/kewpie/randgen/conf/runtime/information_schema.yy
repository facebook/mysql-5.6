# Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software Foundation,
# 51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA

# Last Modification to Grammar : Sandeep D (Bug #11766276)
#  The joins on information schema tables resulted in high usage of temp space (> 350G) . The space consumtion is basically due to sorting temp table created during order by on 
# joins involving tables like INNODB_BUFFER_PAGE . The current modification eliminates the order by on Joins and preserves sorting on projection queries with single table 

query_init:
	SELECT _field FROM _table ;	# Populate the RQG metadata caches from the start of the start of the test

thread1:
	modify ;			# Issue DDL that are reflected in INFORMATION_SCHEMA

query:
	{ @nonaggregates = () ; @table_names = () ; @database_names = () ; $tables = 0 ; $fields = 0 ; "" } select |
	{ @nonaggregates = () ; @table_names = () ; @database_names = () ; $tables = 0 ; $fields = 0 ; "" } select |
	{ @nonaggregates = () ; @table_names = () ; @database_names = () ; $tables = 0 ; $fields = 0 ; "" } select |
	{ @nonaggregates = () ; @table_names = () ; @database_names = () ; $tables = 0 ; $fields = 0 ; "" } select_join |
	{ @nonaggregates = () ; @table_names = () ; @database_names = () ; $tables = 0 ; $fields = 0 ; "" } select_join |
	{ @nonaggregates = () ; @table_names = () ; @database_names = () ; $tables = 0 ; $fields = 0 ; "" } select_join |
	show ;

show:
#	SHOW BINARY LOGS |
	SHOW BINLOG EVENTS |  
	SHOW CHARACTER SET  |
	SHOW COLLATION  |
	SHOW COLUMNS FROM _table |
	SHOW CREATE DATABASE  _letter |
	SHOW CREATE FUNCTION  _letter |
	SHOW CREATE PROCEDURE _letter |
	SHOW CREATE TABLE _letter |
	SHOW CREATE VIEW  _letter |
	SHOW DATABASES  |
#	SHOW ENGINE  |
	SHOW ENGINES  |
	SHOW ERRORS  |
	SHOW FUNCTION CODE _letter |
	SHOW FUNCTION STATUS | 
	SHOW GRANTS  |
	SHOW INDEX FROM _table |
#	SHOW INNODB STATUS  |
#	SHOW LOGS  |
	SHOW MASTER STATUS  |
#	SHOW MUTEX STATUS  |
	SHOW OPEN TABLES  |
	SHOW PRIVILEGES  |
	SHOW PROCEDURE CODE _letter |
	SHOW PROCEDURE STATUS | 
	SHOW PROCESSLIST  |
	SHOW PROFILE  |
	SHOW PROFILES  |
	SHOW SLAVE HOSTS  |
	SHOW SLAVE STATUS  |
	SHOW STATUS  |
	SHOW TABLE STATUS  |
	SHOW TABLES  |
	SHOW TRIGGERS | 
	SHOW VARIABLES | 
	SHOW WARNINGS ;	

modify:
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
	ALTER TABLE _table ADD COLUMN _letter INTEGER |
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
	FROM new_table_item
	where
	group_by
	having
	order_by_limit
;

select_join :
	SELECT *
	FROM join_list
	where
	group_by
	having
	LIMIT _digit
;


select_list:
	new_select_item |
	new_select_item , select_list ;

join_list:
	new_table_item |
	(new_table_item join_type new_table_item ON ( current_table_item . _field = previous_table_item . _field ) ) ;

join_type:
	INNER JOIN | left_right outer JOIN | STRAIGHT_JOIN ;  

left_right:
	LEFT | RIGHT ;

outer:
	| OUTER ;
where:
	WHERE where_list ;

where_list:
	not where_item |
	not (where_list AND where_item) |
	not (where_list OR where_item) ;

not:
	| | | NOT;

where_item:
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
	|
	ORDER BY order_by_list |
	ORDER BY order_by_list LIMIT _digit ;

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
	{ $last_database = $prng->arrayElement(['mysql','INFORMATION_SCHEMA','test']); return $last_database };

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
