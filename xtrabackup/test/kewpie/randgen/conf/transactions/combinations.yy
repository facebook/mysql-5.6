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
#	1		2	3	4	5	6	
	transaction | update | insert | delete | select | replace ;

transaction:
	START TRANSACTION | COMMIT | ROLLBACK | SAVEPOINT A | ROLLBACK TO SAVEPOINT A ;

select:
	SELECT field_list FROM _table where group_by having order_by limit ;

field_list:
	* | _field , _field , _field , _field ;

update:
	UPDATE _table SET _field_no_pk = value where order_by limit ;

insert:
	INSERT INTO _table ( _field_no_pk ) VALUES ( value ) ;

delete:
	DELETE FROM _table where order_by LIMIT _digit ;

replace:
	REPLACE INTO _table ( _field_no_pk ) VALUES ( value ) ;

where:
	|
	WHERE _field operator value ;

group_by:
	|
	GROUP BY _field;

order_by:
	|
	ORDER BY _field;

having:
	|
	HAVING _field operator value ;

limit:
	|
	LIMIT _digit;

operator:
	> | < | >= | <= | <> ;

value:
	_digit | _timestamp | _english ;
