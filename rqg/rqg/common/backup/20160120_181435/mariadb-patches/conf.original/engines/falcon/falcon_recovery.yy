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
	stall_serial_log_rotation | 
	serial_log_event | serial_log_event | serial_log_event | serial_log_event | serial_log_event |
	serial_log_event | serial_log_event | serial_log_event | serial_log_event | serial_log_event |
	serial_log_event | serial_log_event | serial_log_event | serial_log_event | serial_log_event |
	serial_log_event | serial_log_event | serial_log_event | serial_log_event | serial_log_event ;

#
# This creates a transaction in connection_id = 10 that performs an update and then lives for 1800 seconds. 
# This makes serial log rotation impossible, resulting in larger serial log files and longer recovery times.
#

stall_serial_log_rotation:
	START TRANSACTION ; CREATE TEMPORARY TABLE IF NOT EXISTS stall ( `f1` INTEGER , `connection_id` INTEGER ) ENGINE = Falcon ; INSERT INTO stall VALUES (_digit, CONNECTION_ID()) ; UPDATE stall SET f1 = f1 + 1 WHERE connection_id = CONNECTION_ID() ; SELECT IF( CONNECTION_ID() = 10 , SLEEP(1800) , 1 ) ;

serial_log_event:
	blob_delete |
	blob_update |
	checkpoint |
	commit |
	create_index |
	create_section |
	create_tablespace |
	data |
	data_page |
	delete |
	delete_index |
	drop_table |
	drop_tablespace |
	free_page |
	index_add |
	index_delete |
	index_page |
	index_update |
	inversion_page |
	overflow_pages |
	prepare |
	record_locator |
	record_stub |
	rollback |
	savepoint_rollback |
	section_line |
	section_page |
	section_promotion |
	sequence |
	sequence_page |
	session |
	switch_log |
	update_blob |
	update_index |
	update_records |
	version |
	word_update
;

blob_delete:
	DELETE FROM _table WHERE `col_int` = CONNECTION_ID() LIMIT 1 ;

blob_update:
	INSERT INTO _table ( `col_int` , `blob` ) VALUES ( CONNECTION_ID() , _data ) |
	UPDATE _table SET `blob` = _data WHERE `col_int` = CONNECTION_ID() LIMIT _digit ;

checkpoint: ;

commit:
	START TRANSACTION | COMMIT | COMMIT | COMMIT | COMMIT ;

create_index:
	ALTER TABLE _table ADD key_type _letter ( `col_int` ) |
	ALTER TABLE _table ADD key_type _letter ( `col_int` ) |
	ALTER TABLE _table ADD key_type _letter ( `col_char_255` ) |
	ALTER TABLE _table ADD key_type _letter ( `col_char_255` ) ;

key_type:
	INDEX | UNIQUE | PRIMARY KEY ;

create_section:
	CREATE TABLE IF NOT EXISTS _letter (`f1` VARCHAR(255) ) ENGINE = FALCON TABLESPACE _letter ; INSERT INTO _letter SELECT _field FROM _table ;

create_tablespace:
        CREATE TABLESPACE _letter ADD DATAFILE file_name ENGINE = FALCON ;

data: ;

data_page: ;

delete:
	DELETE FROM _table LIMIT 1;

delete_index:
	ALTER TABLE _table DROP INDEX _letter ;
	
drop_table:
	DROP TABLE IF EXISTS _letter ;

#
# This is disabled because of bug 39138 
#
drop_tablespace:
#	DROP TABLESPACE _letter ENGINE = FALCON 
;

free_page:
	TRUNCATE TABLE _letter ;

index_add:
	INSERT INTO _table ( `col_int` ) VALUES ( _digit ) |
	INSERT INTO _table ( `col_char_255` ) VALUES ( _english ) ;

index_delete:
	DELETE FROM _table LIMIT 1;

index_page:
	INSERT INTO _table ( `col_int` ) SELECT `col_int` FROM _table LIMIT _tinyint_unsigned ;

index_update: ;

inversion_page: ;

overflow_pages:
	insert_big_record ; insert_big_record ; insert_big_record ; insert_big_record ; insert_big_record ; insert_big_record ; insert_big_record ;

prepare: ;

record_locator: ;

record_stub: ;

rollback:
	ROLLBACK ;

savepoint_rollback:
	SAVEPOINT A | SAVEPOINT A | SAVEPOINT A | SAVEPOINT A |
	ROLLBACK TO SAVEPOINT A ;

section_line: ;

section_page: ;

section_promotion: ;

sequence:
	INSERT INTO _table (`pk`) VALUE ( NULL ) ;

sequence_page: ;

session: ;

switch_log: ;

update_blob:
	UPDATE _table SET `blob` = _data WHERE `col_int` = CONNECTION_ID() LIMIT _digit |
	INSERT INTO _table (`col_int`, `blob`) VALUES ( CONNECTION_ID(), _data ) ;

update_index:
	INSERT INTO _table (`col_int`) VALUES ( _digit ) ;

update_records:
	UPDATE _table SET `col_int` = `col_int` + 1 LIMIT 1 ;

version: ;

word_update: ;

insert_big_record:
	INSERT INTO _table ( `col_char_255` ) VALUES ( REPEAT('x', 255) ) ;

file_name:
	''f1'' | ''f2'' | ''f3'' | ''f4'' | ''f5'' | ''f6'' | ''f7'' | ''f8'' | ''f9'' | ''f10'' |
	''f11'' | ''f12'' | ''f13'' | ''f14'' | ''f15'' | ''f16'' | ''f17'' | ''f18'' | ''f19'' | ''f20'' ;
