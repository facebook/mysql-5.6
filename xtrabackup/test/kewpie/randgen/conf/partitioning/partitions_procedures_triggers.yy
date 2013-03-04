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
	create_table | drop_table |
	create_procedure | drop_procedure | call_procedure | call_procedure | call_procedure | call_procedure |
	create_trigger | drop_trigger ;

create_table:
	CREATE TABLE IF NOT EXISTS table_name (
		`pk` INTEGER NOT NULL AUTO_INCREMENT ,
		`col_int` INTEGER ,
		PRIMARY KEY ( `pk` )
	) partition SELECT `pk` , `col_int_key` FROM source_table ;

drop_table:
	DROP TABLE IF EXISTS table_name ;

create_view:
	CREATE OR REPLACE VIEW view_name AS SELECT * FROM table_name ;

create_procedure:
	CREATE PROCEDURE procedure_name () BEGIN proc_query ; proc_query ; proc_query ; proc_query ; END ;

drop_procedure:
	DROP PROCEDURE IF EXISTS procedure_name ;

create_trigger:
	CREATE TRIGGER trigger_name trigger_time trigger_event ON table_name FOR EACH ROW BEGIN trigger_body ; END ;

drop_trigger:
	DROP TRIGGER trigger_name ;

trigger_name:
	letter ;

trigger_time:
	BEFORE | AFTER ;

trigger_event:
	INSERT | UPDATE ;

trigger_body:
	call_procedure | insert | update ;

proc_query:
	select | insert | update ;

select:
	SELECT * FROM table_or_view_name ;

insert:
	INSERT INTO table_or_view_name ( `col_int` ) VALUES ( digit ) , ( digit ) , ( digit ) ;

update:
	UPDATE table_or_view_name SET field_name = digit WHERE field_name = digit ;

call_procedure:
	CALL procedure_name ;

partition:
	PARTITION BY partition_hash_or_key;

partition_hash_or_key:
	HASH ( field_name ) partitions |
	KEY ( field_name ) partitions ;

partitions:
	PARTITIONS digit ;

procedure_name:
	letter ;

table_or_view_name:
	table_name | view_name ;

table_name:
	t1 | t2 | t3 | t4 | t5 | t6 | t7 | t8 | t9 | t10 ;

view_name:
	letter;

field_name:
	`pk` | `col_int` ;

source_table:
	D | E ;
