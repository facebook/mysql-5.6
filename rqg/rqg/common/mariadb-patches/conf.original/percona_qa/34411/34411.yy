# Copyright (c) 2008, 2012 Oracle and/or its affiliates. All rights reserved.
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

# Some samples/ideas from other grammars used
# Certain parts (c) Percona Inc

# Suggested use:
# 1. Use this grammar (34411.yy) in combination with 34411.zz & 34411.cc
# 2. Use a duration of 300 to 900 seconds. Short durations ensure fresh/plenty data in the tables
# 3. For use with combinations.pl (assuming a high-end testing server):
#    - 10 RQG threads (--parallel=10) with one SQL thread (--threads=1) for single threaded testing
#    - 8 RQG threads (--parallel=8) with 10-30 SQL threads (--threads=10) for multi threaded testing
#    - Both these over many (400+) trials, both in normal and Valgrind mode, to catch most issues
# 4. You can use --short_column_names option to RQG to avoid overly long column names
# 5. Do not use the --engines option, storage engine assignent is done in percona_qa.zz

# 80% focused feature testing, 20% general queries
query:
	i34411 | i34411 | i34411 | i34411 | query_mix ;

i34411:
	ipv6 | user_stats ;

query_mix:
	select | select | insert | insert | delete | delete | replace | update | transaction | i_s |
        alter | views | set | flush | proc_func | outfile_infile | update_multi | kill_idle |
	drop_create_table | table_comp | optimize_table ;

#INET6_ATON() - Return the numeric value of an IPv6 address in binary format, use HEX() to display in printable form
#INET6_NTOA() - Return the IPv6 address from a numeric value
#IS_IPV4_COMPAT() - Return true if argument is an IPv4-compatible address
#IS_IPV4_MAPPED() - Return true if argument is an IPv4-mapped address
#IS_IPV4() - Return true if argument is an IPv4 address
#IS_IPV6() - Return true if argument is an IPv6 address
ipv6:
	SELECT INET6_ATON( value ) |
	SELECT INET6_NTOA( value ) | 
	SELECT IS_IPV4_COMPAT( value ) | 
	SELECT IS_IPV4_MAPPED( value ) | 	
	SELECT IS_IPV4( value ) |
	SELECT IS_IPV6( value ) ;

user_stats:
	SET GLOBAL USERSTAT = moreon |
	SET GLOBAL THREAD_STATISTICS = moreon |
        SELECT user_stats_1 FROM INFORMATION_SCHEMA.USER_STATISTICS |
        SELECT user_stats_1 FROM INFORMATION_SCHEMA.THREAD_STATISTICS |
        SELECT user_stats_2 FROM INFORMATION_SCHEMA.TABLE_STATISTICS |
        SELECT user_stats_3 FROM INFORMATION_SCHEMA.INDEX_STATISTICS |
        SELECT user_stats_4 FROM INFORMATION_SCHEMA.CLIENT_STATISTICS |
        flush_user_stats | show_user_stats ;

user_stats_1:
        user_stats_5 | user_stats_5 | user_stats_5 | user_stats_5 | user_stats_5 | user_stats_5 |
        USER | TOTAL_CONNECTIONS | CONCURRENT_CONNECTIONS | CONNECTED_TIME | BUSY_TIME | CPU_TIME |
        BYTES_RECEIVED | BYTES_SENT | BINLOG_BYTES_WRITTEN | ROWS_FETCHED | ROWS_UPDATED | TABLE_ROWS_READ |
        SELECT_COMMANDS | UPDATE_COMMANDS | OTHER_COMMANDS | COMMIT_TRANSACTIONS | ROLLBACK_TRANSACTIONS |
        DENIED_CONNECTIONS | LOST_CONNECTIONS | ACCESS_DENIED | EMPTY_QUERIES | TOTAL_SSL_CONNECTIONS |
        user_stats_1 , user_stats_1 | user_stats_1, user_stats_1 | * ;

user_stats_2:
        user_stats_5 | user_stats_5 | 
        TABLE_SCHEMA | TABLE_NAME | ROWS_READ | ROWS_CHANGED | ROWS_CHANGED_X_INDEXES |
        user_stats_2 , user_stats_2 | * ;

user_stats_3:
        user_stats_5 | user_stats_5 | 
        TABLE_SCHEMA | TABLE_NAME | INDEX_NAME | ROWS_READ |
        user_stats_3 , user_stats_3 | * ;

user_stats_4:
        user_stats_5 | user_stats_5 | user_stats_5 | user_stats_5 | user_stats_5 | user_stats_5 |
	CLIENT | TOTAL_CONNECTIONS | CONCURRENT_CONNECTIONS | CONNECTED_TIME | BUSY_TIME | CPU_TIME |
        BYTES_RECEIVED | BYTES_SENT | BINLOG_BYTES_WRITTEN | ROWS_FETCHED | ROWS_UPDATED | TABLE_ROWS_READ |
        SELECT_COMMANDS | UPDATE_COMMANDS | OTHER_COMMANDS | COMMIT_TRANSACTIONS | ROLLBACK_TRANSACTIONS |
        DENIED_CONNECTIONS | LOST_CONNECTIONS | ACCESS_DENIED | EMPTY_QUERIES | TOTAL_CONNECTIONS_SSL |
        user_stats_4 , user_stats_4 | user_stats_4, user_stats_4 | * ;

user_stats_5:
	HANDLER_READ_FIRST | HANDLER_READ_LAST | HANDLER_READ_KEY | HANDLER_READ_NEXT | HANDLER_READ_PREV | 
	HANDLER_READ_RND | HANDLER_READ_RND_NEXT | HANDLER_DELETE | HANDLER_UPDATE | HANDLER_WRITE ;

flush_user_stats:
        FLUSH CLIENT_STATISTICS | FLUSH INDEX_STATISTICS | FLUSH TABLE_STATISTICS | FLUSH THREAD_STATISTICS | FLUSH USER_STATISTICS ;

show_user_stats:
        SHOW CLIENT_STATISTICS  | SHOW INDEX_STATISTICS  | SHOW TABLE_STATISTICS  | SHOW THREAD_STATISTICS  | SHOW USER_STATISTICS ;

i_s_area:
	INFORMATION_SCHEMA.GLOBAL_TEMPORARY_TABLES |
	INFORMATION_SCHEMA.TEMPORARY_TABLES | 
	INFORMATION_SCHEMA.PROCESSLIST ;

i_s:
	SELECT COUNT(*) FROM i_s_area | SELECT * FROM i_s_area |
	SELECT * FROM i_s_area LIMIT _digit ;

scope:
	GLOBAL | SESSION ;

onoff:
	1 | 0 ;	

moreon:
        1 | 1 | 0 ;

set:
	SET scope INNODB_STRICT_MODE = onoff |
	SET scope OLD_ALTER_TABLE = onoff |
	SET @@global.innodb_log_checkpoint_now = TRUE ;

isolation:
	READ-UNCOMMITTED | READ-COMMITTED | REPEATABLE-READ | SERIALIZABLE ;

transaction:
	| | START TRANSACTION | COMMIT | ROLLBACK | SAVEPOINT A | ROLLBACK TO SAVEPOINT A |
	SET scope TX_ISOLATION = isolation ;

select:
	SELECT select_item FROM _table where order_by limit ;
	
select_item:
	_field | _field null | _field op _field | _field sign _field | select_item, _field ;
	
where:
	| WHERE _field sign value | WHERE _field null ;

order_by:
	| ORDER BY _field ;

limit:
	| LIMIT _digit ;
	
null:
	IS NULL | IS NOT NULL ;

op:
	+ | / | DIV ;   # - | * | removed due to BIGINT bug (ERROR 1690 (22003): BIGINT UNSIGNED value is out of range)
	
sign:
	< | > | = | >= | <= | <> | != ;

insert:
	INSERT IGNORE INTO _table ( _field , _field , _field ) VALUES ( value , value , value ) |
	INSERT IGNORE INTO _table ( _field_no_pk , _field_no_pk , _field_no_pk ) VALUES ( value , value , value ) |
	INSERT priority_insert ign INTO _table ( _field ) VALUES ( value ) ON DUPLICATE KEY UPDATE _field_no_pk = value |
	INSERT priority_insert ign INTO _table ( _field ) VALUES ( value ) ON DUPLICATE KEY UPDATE _field = value ;
	
priority_insert:
	| | | | LOW_PRIORITY | DELAYED | HIGH_PRIORITY ;

# Disabled IGNORE due to bug #1168265
#	| | | | IGNORE ;
ign:
	| | | | ;

update:
	UPDATE priority_update ign _table SET _field_no_pk = value where order_by limit ;
	UPDATE priority_update ign _table SET _field_no_pk = value where ;
	UPDATE priority_update ign _table SET _field = value where order_by limit ;
	
update_multi:
	UPDATE priority_update ign _table t1, _table t2 SET t1._field_no_pk = value WHERE t1._field sign value ;

priority_update:
	| | | | | | LOW_PRIORITY ; 

delete:
	| | | | | | | | DELETE FROM _table where order_by limit ;
	
replace:
	REPLACE INTO _table ( _field_no_pk ) VALUES ( value ) ;

table_comp:
	CREATE TABLE IF NOT EXISTS tb_comp ( c1 VARCHAR( vc_size ) null_or_not , c2 VARCHAR( vc_size ) default_or_not , c3 VARCHAR( vcsize ), c4 VARCHAR( vcsize ) null_or_not default_or_not , tb_keydef ) ENGINE = InnoDB ROW_FORMAT = row_format KEY_BLOCK_SIZE = kb_size |
	CREATE TABLE tb_comp ( c1 INTEGER null_or_not AUTO_INCREMENT, c2 DATETIME, c3 DOUBLE, c4 DECIMAL (20,10) , tb_keydef ) ENGINE = InnoDB ROW_FORMAT = row_format KEY_BLOCK_SIZE = kb_size |
	CREATE TABLE tb_comp ( c1 BLOB, c2 TEXT, c3 TIMESTAMP, c4 VARBINARY ( vc_size ) , tb_keydef ) ENGINE = InnoDB ROW_FORMAT = row_format KEY_BLOCK_SIZE = kb_size |
	DROP TABLE tb_comp | DROP TABLE tb_comp | DROP TABLE tb_comp |
	INSERT INTO tb_comp VALUES ( value , value , value , value ) |
	INSERT INTO tb_comp VALUES ( value , value , value , value ) |
	ALTER TABLE tb_comp_plus ROW_FORMAT = row_format |
	ALTER TABLE tb_comp_plus ROW_FORMAT = row_format KEY_BLOCK_SIZE = kb_size |
	ALTER TABLE tb_comp_plus KEY_BLOCK_SIZE = kb_size |
	ALTER TABLE tb_comp_plus DROP PRIMARY KEY |
	ALTER TABLE tb_comp_plus ADD tb_keydef ;

tb_comp:
	t1 | t2 | t3 | t4 | t5 | t6 | t7 | t8 | t9 ;

tb_comp_plus:
	_table | _table | tb_comp ;

row_format:
	COMPRESSED | COMPRESSED | COMPRESSED | COMPRESSED |
	COMPRESSED | COMPRESSED | COMPRESSED | COMPRESSED |
	DEFAULT | DYNAMIC | FIXED | COMPACT ;

tb_keydef:
	PRIMARY KEY (c1) , KEY (c2) hash_or_not |
	PRIMARY KEY (c3,c4) , KEY (c2) hash_or_not |
	PRIMARY KEY (c2) hash_or_not |
	PRIMARY KEY (c4,c3) hash_or_not |
	PRIMARY KEY (c4,c3) hash_or_not KEY_BLOCK_SIZE = kb_size |
	UNIQUE (c4,c3) hash_or_not |
	KEY (c1(1)) ;

hash_or_not:
	| USING HASH | USING BTREE ;

vc_size:
	1 | 2 | 32 | 64 | 1024 ;

kb_size:
	0 | 1 | 2 | 4 | 8 | 16 ;
	
drop_create_table:
	DROP TABLE IF EXISTS _letter[invariant] ; DROP VIEW IF EXISTS _letter[invariant] ; CREATE temp TABLE _letter[invariant] LIKE _table[invariant] ; INSERT INTO _letter[invariant] SELECT * FROM _table[invariant] |
	DROP TABLE IF EXISTS _letter[invariant] ; DROP VIEW IF EXISTS _letter[invariant] ; CREATE temp TABLE _letter[invariant] SELECT * FROM _table |
	DROP TABLE IF EXISTS _letter[invariant] ; DROP VIEW IF EXISTS _letter[invariant] ; CREATE temp TABLE _letter[invariant] LIKE _table[invariant] ; INSERT INTO _letter[invariant] SELECT * FROM _table[invariant] ; DROP TABLE _table[invariant] ; ALTER TABLE _letter[invariant] RENAME _table[invariant] ;
	
optimize_table:
	OPTIMIZE TABLE _table |
	OPTIMIZE NO_WRITE_TO_BINLOG TABLE _table |
	OPTIMIZE LOCAL TABLE _table ;

temp:
	| | | | | TEMPORARY ;

type:
	INT | DECIMAL | FLOAT | BIT | CHAR( _digit ) | VARCHAR ( _digit ) | BLOB | BLOB | BLOB |
	DATE | DATETIME | TIMESTAMP | TIME | YEAR | BINARY | TEXT | ENUM('a','b','c') | SET('a','b','c') ;

null_or_not:
	| | NULL | NOT NULL ;

default_or_not:
	| | DEFAULT 0 | DEFAULT NULL | DEFAULT 1 | DEFAULT 'a' ;

after_or_not:
	| | AFTER _field | FIRST ;

# Errors: fix later (see above)
#	ALTER TABLE _table algo lock_type MODIFY _field type null_or_not default_or_not after_or_not |
#	ALTER TABLE _table algo lock_type ALTER _field DROP DEFAULT |
#	ALTER TABLE _table algo lock_type CHANGE _field c1 type null_or_not default_or_not after_or_not ;

alter:
	ALTER TABLE _table MODIFY _field type null_or_not default_or_not after_or_not |
	ALTER TABLE _table ALTER _field DROP DEFAULT |
	ALTER TABLE _table CHANGE _field c1 type null_or_not default_or_not after_or_not ;

proc_func:
	DROP PROCEDURE IF EXISTS _letter[invariant] ; CREATE PROCEDURE _letter[invariant] ( proc_param ) BEGIN SELECT COUNT( _field ) INTO @a FROM _table ; END ; CALL _letter[invariant](@a); |
	DROP FUNCTION IF EXISTS _letter[invariant] ; CREATE FUNCTION _letter[invariant] ( _letter type ) RETURNS type DETERMINISTIC READS SQL DATA BEGIN DECLARE out1 type ; SELECT _table._field INTO out1 FROM _table ; RETURN out1 ; END ; CALL _letter[invariant](@a);

flush:
        FLUSH TABLES | FLUSH TABLES | FLUSH TABLES | FLUSH QUERY CACHE | FLUSH QUERY CACHE |
        FLUSH TABLE _table | FLUSH TABLE _letter ;

# 89% unlocking, 11% locking functions
locking:
	UNLOCK TABLES | UNLOCK TABLES | UNLOCK TABLES | UNLOCK TABLES | UNLOCK TABLES |
	UNLOCK TABLES | UNLOCK TABLES | UNLOCK TABLES | UNLOCK TABLES | lock_function ;

lock_function:
        LOCK TABLE _table READ | LOCK TABLE _table WRITE |
        LOCK TABLE _letter READ | LOCK TABLE _letter WRITE |
        LOCK TABLE _table AS _letter READ | LOCK TABLE _table as _letter WRITE |
        LOCK TABLE _table READ LOCAL | LOCK TABLE _table LOW_PRIORITY WRITE |
        LOCK TABLE _table AS _letter READ LOCAL | LOCK TABLE _table as _letter LOW_PRIORITY WRITE |
        FLUSH TABLES WITH READ LOCK ;

proc_param:
	IN _letter type | OUT _letter type ;

views:
	DROP TABLE IF EXISTS _letter[invariant] ; DROP VIEW IF EXISTS _letter[invariant] ; CREATE VIEW _letter[invariant] AS SELECT * FROM _table ; INSERT INTO _letter[invariant] ( _field ) VALUES ( value ) ;
	
outfile_infile:
	SELECT * FROM _table[invariant] INTO OUTFILE _tmpnam ; TRUNCATE _table[invariant] ; LOAD DATA INFILE _tmpnam INTO TABLE _table[invariant] ;
	SELECT * FROM _table[invariant] INTO OUTFILE _tmpnam ; TRUNCATE _table[invariant] ; LOAD DATA LOCAL INFILE _tmpnam INTO TABLE _table[invariant] ;

value:
	_digit | 0 | 1 | -1 | _data | _bigint_unsigned | _bigint | _mediumint | _english | _letter | 
	_char | _varchar |_date | _year | _time | _datetime | _timestamp | NULL | NULL | NULL | 
	ipval | ipval | ipval | ipval | ipval | ipval | ipval | ipval ;

ipval:
	'0.0.0.0' | '1.1.1.1' | '255.255.255.255' | '256.255.255.255' | '255.256.255.255' | '255.255.256.255' | '255.255.255.256' |
	'NULL.0.0.0' | '999999999999999999999999999.255.255.255' | '128.128.128.128' | '127.127.127.127' | 'a.b.c.d' | '128.128.128.c' |
	'999999999999999999999999999.999999999999999999999999999.999999999999999999999999999.999999999999999999999999999' | '/' | '\' |
	'130.130.130.130/250' | '-1.1.1.1' | '192.0.0.0' | '192.0.0.0.0' | '192..0.0' | '...' | '192.NULL.a.-9999999999999999/50' |
	'10.10.010.050/24' | '10.*.0.0' | '1.1' | '100.100.100.100.100' | '(100.100.100.100)' | '10.10.10.10/-1' | '10.10.10.10/10.10' |
	'2001:0:9d38:6abd:3c3e:1e4:3f57:d4e9' | 'fe80::3c3e:1e4:3f57:d4e9%14' | '2600:f0e0:1000:33::2' | '2600:f0e0:1000:33::2/24' |
	'0000:0000:0000:0000:0000:0000:0000:0000' | '0000:0000:0000:0000:0000:0000:0000:000z' | '2001:-0:9d38:6abd:3c3e:1e4:3f57:d4e9' | 
	'fe80::61a1:d0c3:a72f:b441-17' | '2001:0:9d38:6abd:3c3e:1e4:3f57:d4x9' | 'fe80::3c3e:1e4:3f57:d4e9_14' | 'fe80::3c3e:1e4:3f57:d4e9:250' | 
	'fe80::3c3e:1e4:3f57:d4e9_14:' | 'FFFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF' | '::' ;
