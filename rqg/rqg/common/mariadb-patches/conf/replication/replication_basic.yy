# Copyright (c) 2012 Oracle and/or its affiliates. All rights reserved.
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

#############################################################
#
# Author:	Serge Kozlov <serge.kozlov@oracle.com>
# Date:		19-Feb-2012
# Purpose:	Basic replicaton grammar
# Key points:
#		1. Grammar creates databases 
#		'test_1' .. 'test_4' and copy tables from 
#		database 'test'.
#		2. Outside of transaction grammar changes
#		current database randomly choosed 
#		database.
#############################################################

query_init:
	{ $db= 0; $trx= "BEGIN"; undef } ; create_db ; create_db ; create_db ; create_db ;

create_db:
	{ $db++; undef } CREATE DATABASE IF NOT EXISTS { 'test_'.$db } ; create_tables ; copy_tables ;

create_tables:
	{ $next_db= join(',', @{$executors->[0]->tables()}); $next_db .= ','; $next_db =~ s/([a-zA-Z0-9_]+)\,/CREATE TABLE IF NOT EXISTS test_$db.$1 LIKE $1;/g; $next_db } ; 

copy_tables:
	{ $next_db= join(',', @{$executors->[0]->tables()}); $next_db .= ','; $next_db =~ s/([a-zA-Z0-9_]+)\,/INSERT INTO test_$db.$1 SELECT * FROM $1;/g; $next_db } ; 
	

query:
	trx_begin_end | 
	trx_statement | 
	trx_statement | 
	trx_statement | 
	trx_statement | 
	trx_statement | 
	trx_statement | 
	trx_statement | 
	trx_statement | 
	trx_statement | 
	trx_statement | 
	trx_savepoint |
	flush_log |
	master_statement |
	change_db ;

trx_begin_end:
	$trx ; { $trx =~ s/BEGIN/tempCOMMIT/g; $trx =~ s/^COMMIT/BEGIN/g; $trx =~ s/^ROLLBACK/BEGIN/g; $trx =~ s/temp//g; if ($trx == "COMMIT" && rand() > 0.8) { $trx =~ s/^COMMIT/ROLLBACK/g } ; if ($trx == "BEGIN") { $savepoint= "ROLLBACK TO SP1"}; undef } ;	

trx_savepoint:
	SAVEPOINT SP1 | ROLLBACK TO SP1 ;

trx_statement: 
	select_into |
	insert | 
	insert | 
	insert | 
	insert_multi |
	insert_select |
	update |
	update |
	delete ;

change_db:
	{ $change_db= undef; if ($trx == "BEGIN") { $change_db= "USE test"; if (rand() > 0.2) { $change_db .= "_" . int(rand($db) + 1)}; }; $change_db } ;

select_into:
	SELECT * INTO OUTFILE _tmpnam FROM _table condition ;
	
update:
 	UPDATE _table SET _field = digit condition |
 	UPDATE _table SET _field = value, _field = value condition ;

delete:
	DELETE FROM _table condition ;

insert:
	INSERT INTO _table ( _field ) VALUES ( value ) |
	INSERT IGNORE INTO _table ( _field ) VALUES ( value ); 
	
insert_multi:
	INSERT INTO _table ( _field ) VALUES ( value ), ( value ) , ( value ) , ( value ) , ( value ) |
	INSERT IGNORE INTO _table ( _field ) VALUES ( value ), ( value ) , ( value ) , ( value ) , ( value ) ; 
	
insert_select:	
	INSERT INTO _table SELECT * FROM _table condition |
	INSERT IGNORE INTO _table SELECT * FROM _table condition ;
	
condition:
	WHERE part_where | 
	WHERE part_where ORDER BY part_orderby | 
	WHERE part_where LIMIT part_limit | 
	WHERE part_where ORDER BY part_orderby LIMIT part_limit |
	WHERE part_subquery |
	WHERE part_subquery OR part_where ;

part_subquery:
	part_subquery_comp |
	part_subquery_all |
	part_subquery_exists ;
	
part_subquery_comp:
	_field = (SELECT COUNT(*) FROM _table WHERE part_where |
	_field >= (SELECT MAX(_field) FROM _table WHERE part_where |
	_field = (SELECT _field FROM _table WHERE part_where LIMIT 0,1) |
	_field > (SELECT _field FROM _table WHERE part_where LIMIT 0,1) |
	_field <= (SELECT _field FROM _table WHERE part_where LIMIT 0,1) ;

part_subquery_all:
	_field > ALL (SELECT _field FROM _table) | 
	_field < ALL (SELECT _field FROM _table WHERE part_where) ;
				
part_subquery_exists:
	EXISTS (SELECT * FROM _table WHERE part_where) |
	EXISTS (SELECT * FROM _table WHERE part_where GROUP BY part_groupby) |
	NOT EXISTS (SELECT * FROM _table WHERE part_where) |
	NOT EXISTS (SELECT * FROM _table WHERE part_where GROUP BY part_groupby) ;

part_where:
	part_where_item |
	part_where_item AND (part_where_item) |
	part_where_item OR ((part_where_item) AND (part_where_item)) ;
	
part_where_item:
 	_field = value |
	_field > value |
	_field >= value |
	_field < value |
	_field <= value |
	_field <> value |
	_field IN ( value, value, value, value, value ) |
	_field BETWEEN value AND value |
	( _field = value OR _field = value ) AND _field < value |
	_field IS NOT NULL |
	DATE_ADD(_datetime, INTERVAL _tinyint SECOND) < NOW() |
	LOG(_tinyint_unsigned) > _field |
	_int_unsigned > RAND(_int_unsigned) ;
	
part_orderby:
	_field | 
	_field DESC | 
	_field, _field DESC ;

part_groupby:
	_field_no_pk |
	_field | 
	_field_no_pk,_field ;
		
part_limit:
	_tinyint_unsigned,_tinyint_unsigned;	
value:
	simple_value |
	simple_value |
	simple_value |
	simple_value |
	complex_value ;
	
simple_value: 
	_digit | _char(255) | _english | _datetime | NULL | _data ;

complex_value:
	(_field + _field[invariant]) |
	REPEAT(_tinyint_unsigned, _english) |
	DATEDIFF(NOW(), _date) ;

flush_log:
	FLUSH LOGS;
	
master_statement:
	SHOW BINLOG EVENTS | 
	SHOW MASTER STATUS ;
