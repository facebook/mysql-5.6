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

query_init:
	create ; create ; create ; create ; create ; create ; create ; create ; create ; create ;

query:
	dml | dml | dml |
	dml | dml | dml |
	dml | dml | dml |
	ddl;

ddl:
	create | create | create | create | drop ;

dml:
	select;

select:
	SELECT * FROM pick_existing_table ;

create:
	CREATE TABLE IF NOT EXISTS pick_create_table (F1 INTEGER) ;

drop:
	DROP TABLE IF EXISTS pick_drop_table ;

pick_create_table:
	{ if (scalar(@dropped_tables) > 0) { $created_table = shift @dropped_tables } else { $created_table = $prng->letter() } ; push @created_tables, $created_table ; $created_table } ;

pick_drop_table:
	{ if (scalar(@created_tables) > 0) { $dropped_table = pop @created_tables } else { $dropped_table = $prng->letter() } ; push @dropped_tables, $dropped_table ; $dropped_table } ;

pick_existing_table:
	{ if (scalar(@created_tables) > 0) { $prng->arrayElement(\@created_tables) } else { $prng->letter() } } ;
