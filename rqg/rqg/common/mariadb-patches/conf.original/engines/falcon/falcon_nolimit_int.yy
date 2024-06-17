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

query:
	select | select | select | select | select |
	select | select | select | select | select |
	select | select | select | select | select |
	dml | dml | dml | dml | dml |
	transaction ;

dml:
	update | insert | delete ;

select:
	SELECT * FROM _table where;

where:
	|
	WHERE _field sign value ;
#	WHERE _field BETWEEN value AND value |
#	WHERE _field IN ( value , value , value , value , value , value ) ;

sign:
	> | < | = | >= | <= ;

insert:
	INSERT INTO _table ( _field , _field ) VALUES ( value , value ) ;

update:
	UPDATE _table AS X SET _field = value where ;

delete:
	DELETE FROM _table where LIMIT digit ;

transaction: START TRANSACTION | COMMIT | ROLLBACK ;

alter:
	ALTER ONLINE TABLE _table DROP KEY letter |
	ALTER ONLINE TABLE _table DROP KEY _field |
	ALTER ONLINE TABLE _table ADD KEY letter ( _field ) |
	ALTER ONLINE TABLE _table ADD KEY letter ( _field ) ;

value:
	_digit | _tinyint_unsigned ;

# Use only indexed fields:

_field:
	`col_int_key` ;
