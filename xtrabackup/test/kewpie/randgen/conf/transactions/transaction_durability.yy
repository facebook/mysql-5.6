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
	lock ; START TRANSACTION ; dml ; dml ; SAVEPOINT A ; dml ; dml ; ROLLBACK TO SAVEPOINT A ; dml ; dml ; commit_rollback ; unlock ;

lock:
	SELECT GET_LOCK('LOCK', 65535) ;

unlock:
	SELECT RELEASE_LOCK('LOCK') ;

commit_rollback:
	COMMIT | ROLLBACK ;

dml:
	insert | update ;
#| delete ;

where:
	WHERE _field sign value ;
#	WHERE _field BETWEEN value AND value ;

sign:
	= | > | < | = | >= | <> | <= | != ;

insert:
	INSERT INTO _table ( _field , _field ) VALUES ( value , value ) ;

update:
	UPDATE _table AS X SET _field = value , _field = value , _field = value where ;

delete:
	DELETE FROM _table WHERE _field = value ;

value:
	_tinyint_unsigned ;

_field:
	`col_int_key` | `col_int_nokey` ;

# | _english | _digit | _date | _datetime | _time ;
