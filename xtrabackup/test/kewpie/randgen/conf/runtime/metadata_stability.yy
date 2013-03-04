# Copyright (c) 2008, 2011 Oracle and/or its affiliates. All rights reserved.
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
# The point of this grammar is to test repeatable read for transactional storage
# engines. It is designed to be used with the SelectStability validator.
# See http://forge.mysql.com/wiki/RandomQueryGeneratorValidators for more info.
#

query_init:
	START TRANSACTION ;

query:
	transaction |
	select | select | call |
	dml | dml |
	ddl ;

transaction:
	START TRANSACTION | COMMIT | ROLLBACK ;

select:
	SELECT /* QUERY_ID: _mediumint_unsigned */ /* RESULTSET_SAME_DATA_IN_EVERY_ROW */ select_item AS `col_int_key`
	FROM join_list
	where;

call:
	CALL /* RESULTSET_SAME_DATA_IN_EVERY_ROW */ procedure_name ( @inout1 ) |
	SET @inout1 = 0 ; CALL procedure_name ( @inout1 ) ; SELECT /* RESULTSET_SINGLE_INTEGER_ONE */ IF(@inout1 IN (0,1), 1, 999);

dml:
	update | 
        # Bug#46746 Innodb transactional consistency is broken with UPDATE + DELETE
        # Disabled: DELETE
        # delete |
	insert_select ;

update:
	pick_table_name UPDATE $table_name SET `col_int_key` = _digit ;

insert_select:
	pick_table_name INSERT INTO $table_name SELECT * FROM $table_name LIMIT 0 ;

delete:
	pick_table_name DELETE FROM $table_name LIMIT 1 ;

select_item:
	_digit |
	function_name ( _digit ) |
	function_name ( `col_int_key` ) ;

join_list:
	selectable_object;

selectable_object:
	_table |
	pick_table_name $table_name |
	pick_view_name $view_name ;

where:
	|
	WHERE argument operator argument;

argument:
	`col_int_key` |
	_digit |
	function_name ( argument ) ;

operator:
	< | <> ;

ddl:
	table_ddl |
	function_ddl |
	table_ddl |
	view_ddl |
	procedure_ddl |
	trigger_ddl;

table_ddl:
	pick_table_name create_table | pick_table_name create_table | pick_table_name create_table |
	pick_table_name alter_table ;
	pick_table_name drop_table ; create_table ;

function_ddl:
	pick_function_name create_function | pick_function_name create_function | pick_function_name create_function |
	pick_function_name drop_function ; create_function ;

procedure_ddl:
	pick_procedure_name create_procedure | pick_procedure_name create_procedure | pick_procedure_name create_procedure |
	pick_procedure_name drop_procedure ; create_procedure ;

trigger_ddl:
	pick_trigger_name create_trigger | pick_trigger_name create_trigger | pick_trigger_name create_trigger |
	pick_trigger_name drop_trigger ; create_trigger ;

view_ddl:
	pick_view_name create_view | pick_view_name create_view | pick_view_name create_view |
        pick_view_name alter_view | pick_view_name drop_view ; create_view ;

pick_function_name:
	{ $function_name = 'func_'.$prng->int(1,3) ; return undef } ;

pick_procedure_name:
	{ $procedure_name = 'proc_'.$prng->int(1,3) ; return undef } ;

pick_table_name:
	{ $table_name = $prng->arrayElement($executors->[0]->tables()) ; return undef } |
	{ $table_name = 'table_'.$prng->int(1,3) ; return undef } ;

pick_trigger_name:
	{ $trigger_name = 'trigger_'.$prng->int(1..9) ; return undef } ;

pick_view_name:
	{ $view_name = 'view_'.$prng->int(1..3) ; return undef } ;

function_name:
	{ 'func_'.$prng->int(1,3) } ;

procedure_name:
	{ 'proc_'.$prng->int(1,3) } ;

create_function:
	CREATE FUNCTION $function_name (in1 INTEGER) RETURNS INTEGER function_body ;

create_procedure:
	CREATE PROCEDURE $procedure_name (INOUT inout1 INT) procedure_body ;

create_table:
	CREATE temporary TABLE $table_name ( `col_int_key` INTEGER, KEY (`col_int_key`) ) select ;

create_view:
	CREATE OR REPLACE ALGORITHM = view_algorithm VIEW $view_name AS select ;

alter_view:
	ALTER ALGORITHM = view_algorithm VIEW $view_name AS select ;

drop_view:
	DROP VIEW $view_name ;

view_algorithm:
	UNDEFINED | MERGE | TEMPTABLE ;

temporary:
	| TEMPORARY;

create_trigger:
	pick_table_name CREATE TRIGGER $trigger_name trigger_time trigger_event ON $table_name FOR EACH ROW update ;

trigger_time:
	BEFORE | AFTER;

trigger_event:
	INSERT ;

drop_trigger:
	DROP TRIGGER $trigger_name;

drop_table:
	DROP TABLE IF EXISTS $table_name;

alter_table:
	ALTER TABLE $table_name ADD COLUMN `default` INTEGER DEFAULT _digit |
	ALTER TABLE $table_name CHANGE COLUMN `default` `default` INTEGER DEFAULT _digit ;

drop_function:
	DROP FUNCTION $function_name ;

drop_procedure:
	DROP PROCEDURE $procedure_name ;

function_body:
	RETURN _digit |
	RETURN in1 ;

procedure_body:
	BEGIN SELECT COUNT(DISTINCT select_item ) INTO inout1 FROM join_list where ; END |
	BEGIN SELECT select_item FROM join_list where ; END ;
