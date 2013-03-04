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
	select | dml | dml | 
	select | dml | dml | 
	select | dml | dml | 
	select | dml | dml | 
	select | dml | dml | transaction ;
dml:
	update | insert | insert | insert | delete ;

select:
	SELECT _field FROM _table where order_by limit;

where:
	|
	WHERE _field < value |
	WHERE _field > value |
	WHERE _field = value ;

order_by:
	| 
	ORDER BY _field ;

limit:
	|
	LIMIT digit ;
	
insert:
	INSERT INTO _table ( _field , _field ) VALUES ( value , value ) ;

update:
	UPDATE _table SET _field = value where order_by limit ;

delete:
	DELETE FROM _table where LIMIT digit ;

transaction:
	START TRANSACTION | COMMIT | ROLLBACK ;

value:
	REPEAT( value_one , tinyint_unsigned ) ;

value_one:
	' letter ' | digit | _date | _datetime | _time ;
