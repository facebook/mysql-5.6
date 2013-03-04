# Copyright (C) 2008 Sun Microsystems, Inc. All rights reserved.
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

query:
	transaction | select |
	dml | dml | dml | dml | dml ;

transaction:
	START TRANSACTION | COMMIT | ROLLBACK ;

select:
	SELECT select_item FROM _table WHERE _field sign value ;

select_item:
	_field |
	_field null |
	_field op _field |
	_field sign _field ;

null:
	IS NULL | IS NOT NULL ;

op:
	+ | - | * | / | DIV ;

dml:
	insert | update | delete ;

insert:
	INSERT IGNORE INTO _table ( _field , _field , _field ) VALUES ( value , value , value );

update:
	UPDATE _table SET _field = value WHERE _field sign value ;

delete:
	DELETE FROM _table WHERE `pk` = value ;

sign:
	< | > | = | >= | <= | <> | != ;

value:
	_english | _digit | NULL | _bigint_unsigned | _bigint | _date | _time | _datetime | _timestamp | _year | _char(64) | _mediumint ;
