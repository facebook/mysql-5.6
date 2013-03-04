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
 	update | insert | delete | select ;

select:
	SELECT _field FROM _table ;

update:
 	UPDATE _table SET _field_no_pk = value WHERE condition update_scope;

update_scope:
	|
	ORDER BY `pk` LIMIT _digit ;

delete:
	DELETE FROM _table WHERE condition ORDER BY `pk` LIMIT 1 ;

insert:
	INSERT INTO _table ( _field , _field , _field ) VALUES ( value , value , value ) ;

value:
	CONVERT( string , CHAR) |
	REPEAT( _hex , _tinyint_unsigned );

string:
	_english | _varchar(255);

condition:
	_field operator value |
	_field BETWEEN value AND value |
	_field IN ( value , value , value , value , value , value , value ) |
	_field LIKE CONCAT( value , '%' ) |
	_field IS not NULL ;

not:
	| NOT ;

operator:
	< | > | = | <> | != | <= | >= ;
