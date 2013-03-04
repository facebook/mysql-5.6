# Copyright (C) 2010 Sun Microsystems, Inc. All rights reserved.
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

# NOTE:  gendata file = drizzle.zz
# This doesn't really test collations at all
# Need to update / fix this test
# However, it does beat up UPDATE / DELETE ;

query:
	delete | insert | update ;

xid_event:
	START TRANSACTION | COMMIT ;

# We need to beef up INSERT here, as the NOT NULL columns don't have
# default values - thus INSERTS fail a lot
insert:
	INSERT INTO table_name ( field_name ) VALUES ( _char ) ;

update:
	UPDATE table_name SET field_name = _char  WHERE field_name oper _char ;

delete:
	DELETE FROM table_name WHERE field_name oper _char ;

table_name:
	_table ;

field_name:
	`col_char_10_key` | `col_char_10` | `col_char_10_not_null` | `col_char_10_not_null_key` |
        `col_char_1024_key` | `col_char_1024` | `col_char_1024_not_null` | `col_char_1024_not_null_key` ;

oper:
	= | > | < | >= | <= | <> ;

