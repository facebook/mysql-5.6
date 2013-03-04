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
	dml | dml | dml | dml | dml |
	transaction |
	select | select | select | select | select |
	dml | dml | dml | dml | dml |
	transaction |
	select | select | select | select | select |
	dml | dml | dml | dml | dml |
	transaction |
	alter 
;

dml:
	update | insert | delete ;

select:
	SELECT * FROM _table where order_by limit;

where:
	|
	WHERE _field sign value |
	WHERE _field BETWEEN value AND value ;
#	WHERE _field IN ( value , value , value , value , value , value ) ;

sign:
	> | < | = | >= | <> | <= | != ;

order_by:
	ORDER BY _field , `pk` ;

limit:
	LIMIT _digit | LIMIT _tinyint_unsigned | LIMIT 65535 ;

insert:
	INSERT INTO _table ( _field , _field ) VALUES ( value , value ) ;

update:
	UPDATE _table AS X SET _field = value where order_by limit;

delete:
	DELETE FROM _table where order_by LIMIT digit ;

transaction: START TRANSACTION | COMMIT | ROLLBACK ;

alter:
	ALTER ONLINE TABLE _table DROP KEY letter |
	ALTER ONLINE TABLE _table DROP KEY _field |
	ALTER ONLINE TABLE _table ADD KEY letter ( _field ) |
	ALTER ONLINE TABLE _table ADD KEY letter ( _field ) ;

value:
	_english | _digit | _date | _datetime | _time ;

# Use only indexed fields:

_field:
	`col_int_key` | `col_date_key` | `col_datetime_key` | `col_varchar_key` ;
