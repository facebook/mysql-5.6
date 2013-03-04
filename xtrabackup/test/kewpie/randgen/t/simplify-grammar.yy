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
 	select | update | insert | delete ;

select:
	SELECT select_items FROM _table where group_by order_by limit;

limit:
	| LIMIT _digit ;

order_by:
	| ORDER BY _field ;

where:
	| WHERE condition ;

group_by:
	| GROUP BY _field ;

select_items:
	select_item |
	select_items , select_item ;

select_item:
	S1 | S2 | S3 ;

update:
 	UPDATE _table SET _field = digit where limit ;

delete:
	DELETE FROM _table WHERE condition limit ;

insert:
	INSERT INTO _table ( _field ) VALUES ( _digit ) ;

condition:
 	cond_item < digit | cond_item = _digit ;

cond_item:
	C1 | C2 | C3 ;

_field:
	F1 | F2 | F3 ;

_table:
	AA | BB | CC ;
