# Copyright (C) 2008 Sun Microsystems, Inc. All rights reserved.
# Copyright (c) 2013, Monty Program Ab.
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
# This grammar is a slightly modified version of engine_stress.yy,
# suitable for Galera testing
#

query:
	transaction |
	select | select |
	select | select |
	insert_replace | update | delete |
	insert_replace | update | delete |
	insert_replace | update | delete |
	insert_replace | update | delete |
	insert_replace | update | delete ;

transaction:
	START TRANSACTION |
	COMMIT ; SET TRANSACTION ISOLATION LEVEL isolation_level |
	ROLLBACK ; SET TRANSACTION ISOLATION LEVEL isolation_level |
	SAVEPOINT A | ROLLBACK TO SAVEPOINT A |
	SET AUTOCOMMIT=OFF | SET AUTOCOMMIT=ON ;

isolation_level:
	READ UNCOMMITTED | READ COMMITTED | REPEATABLE READ | SERIALIZABLE ;

select:
	SELECT select_list FROM join_list where LIMIT large_digit;

select_list:
	X . _field_key | X . _field_key |
	X . `pk` |
	X . _field |
	* |
	( subselect );

subselect:
	SELECT _field_key FROM _table WHERE `pk` = value ;

# Use index for all joins
join_list:
	_table AS X | 
	_table AS X LEFT JOIN _table AS Y USING ( _field_key );


# Insert more than we delete
insert_replace:
	i_r INTO _table (`pk`) VALUES (NULL) |
	i_r INTO _table ( _field_no_pk , _field_no_pk ) VALUES ( value , value ) , ( value , value ) |
	i_r INTO _table ( _field_no_pk ) SELECT _field_key FROM _table AS X where ORDER BY _field_list LIMIT large_digit;

i_r:
	INSERT ignore |
	REPLACE;

ignore:
	| 
	IGNORE ;

update:
	UPDATE ignore _table AS X SET _field_no_pk = value where ORDER BY _field_list LIMIT large_digit ;

# We use a smaller limit on DELETE so that we delete less than we insert

delete:
	DELETE ignore FROM _table where_delete ORDER BY _field_list LIMIT small_digit ;

order_by:
	| ORDER BY X . _field_key ;

# Use an index at all times
where:
	|
	WHERE X . _field_key < value | 	# Use only < to reduce deadlocks
	WHERE X . _field_key IN ( value , value , value , value , value ) |
	WHERE X . _field_key BETWEEN small_digit AND large_digit |
	WHERE X . _field_key BETWEEN _tinyint_unsigned AND _int_unsigned |
	WHERE X . _field_key = ( subselect ) ;

where_delete:
	|
	WHERE _field_key = value |
	WHERE _field_key IN ( value , value , value , value , value ) |
	WHERE _field_key IN ( subselect ) |
	WHERE _field_key BETWEEN small_digit AND large_digit ;

large_digit:
	5 | 6 | 7 | 8 ;

small_digit:
	1 | 2 | 3 | 4 ;

value:
	_digit | _tinyint_unsigned | _varchar(1) | _int_unsigned ;

zero_one:
	0 | 0 | 1;
