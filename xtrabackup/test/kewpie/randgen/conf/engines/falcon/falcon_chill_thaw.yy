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
	transaction |
	dml | dml | dml | dml | dml | dml | dml | dml |
	dml | dml | dml | dml |	dml | dml | dml | dml |
	dml | dml | dml | dml |	dml | dml | dml | dml |
	dml | dml | dml | dml |	dml | dml | dml | dml |
	dml | dml | dml | dml ;

dml:
	update | insert | select;

where_cond_2:
	X . _field < value | X . _field > value;

where_cond_1:
	_field < value | _field > value ;

select:
	SELECT * FROM _table WHERE where_cond_1 ;

insert:
	INSERT INTO _table ( _field , _field ) VALUES ( value , value ) ;

update:
	UPDATE _table AS X SET _field = value WHERE where_cond_2 ;

transaction:
	START TRANSACTION | COMMIT | ROLLBACK ;

value:
	_char(255) | _bigint ;
