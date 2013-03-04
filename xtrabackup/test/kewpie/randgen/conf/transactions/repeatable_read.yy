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

query_init:
	SET GLOBAL TRANSACTION ISOLATION LEVEL REPEATABLE READ ; SET AUTOCOMMIT=OFF ;

query:
	transaction | insert | update | delete | select ;
transaction:
	START TRANSACTION | COMMIT | ROLLBACK | SAVEPOINT A | ROLLBACK TO SAVEPOINT A ;

update:
 	UPDATE _table SET _field = _digit WHERE condition LIMIT _digit ;

delete:
	DELETE FROM _table WHERE condition LIMIT _digit ;

insert:
	INSERT INTO _table ( `col_int_key`, `col_int` ) VALUES ( _digit , _digit ) ;

condition:
	1 = 1 |
	_field < _digit |
	_field = _digit |
	_field > _digit |
	_field BETWEEN _digit and _digit |
	_field IN ( _digit , _digit , _digit );

select:
	SELECT _field FROM _table WHERE condition ;
