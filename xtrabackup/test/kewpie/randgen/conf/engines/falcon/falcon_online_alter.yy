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
	update | insert | select | alter | transaction ;

select:
	SELECT _field FROM _table WHERE where_cond group_by limit;

group_by:
	| GROUP BY _field ;

limit:
	| LIMIT digit ;

where_cond:
	_field < digit;

insert:
	INSERT INTO _table ( _field , _field ) VALUES ( digit , digit ) ;

update:
#	UPDATE _table SET _field = digit WHERE where_cond limit 
;

delete:
	DELETE FROM _field WHERE where_cond LIMIT digit;

transaction:
	START TRANSACTION | COMMIT | ROLLBACK;

alter:
	ALTER online TABLE _table key_def , key_def |
	ALTER online TABLE _table DROP KEY letter ;

online:
	 ;

key_def:
	ADD key_type letter ( _field , _field );

key_type:
	KEY | UNIQUE | PRIMARY KEY ;
