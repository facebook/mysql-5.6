# Copyright (C) 2009-2010 Sun Microsystems, Inc. All rights reserved.
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

# From the manual:
##################
# 5.2.4.3. Mixed Binary Logging Format
# ... automatic switching from statement-based to row-based replication takes place under the following conditions:
# DML updates an NDBCLUSTER table
# When a function contains UUID().
#   --> "value_unsafe_for_sbr"
# When 2 or more tables with AUTO_INCREMENT columns are updated.
#   --> "update","delete"
# When any INSERT DELAYED is executed.
#   --> "low_priority_delayed_high_priority" but this had to be disabled.
# When the body of a view requires row-based replication, the statement creating the view also uses it — for example, this occurs when the statement creating a view uses the UUID() function.
# When a call to a UDF is involved.
# If a statement is logged by row and the client that executed the statement has any temporary tables, then logging by row is used for all subsequent statements
#    (except for those accessing temporary tables) until all temporary tables in use by that client are dropped.  This is true whether or not any temporary tables are actually logged.
#    Temporary tables cannot be logged using the row-based format; thus, once row-based logging is used, all subsequent statements using that table are unsafe,
#    and we approximate this condition by treating all statements made by that client as unsafe until the client no longer holds any temporary tables.
# When FOUND_ROWS() or ROW_COUNT() is used. (Bug#12092, Bug#30244)
#        SELECT SQL_CALC_FOUND_ROWS * FROM tbl_name
#        WHERE id > 100 LIMIT 10;
#        SELECT FOUND_ROWS();
#    FOUND_ROWS() or ROW_COUNT() are bigint(21) ;
#    Without SQL_CALC_FOUND_ROWS within the previous SELECT, FOUND_ROWS() = number of rows found by this SELECT.
#    --> "value_unsafe_for_sbr", but no SQL_CALC_FOUND_ROWS
# When USER(), CURRENT_USER(), or CURRENT_USER is used. (Bug#28086)
#    --> "value_unsafe_for_sbr"
# When a statement refers to one or more system variables. (Bug#31168)
#     --> "shake_clock" affects timestamp
#
# Exception. The following system variables, when used with session scope (only), do not cause the logging format to switch:
#   * auto_increment_increment * auto_increment_offset
#   * character_set_client * character_set_connection * character_set_database * character_set_server * collation_connection * collation_database * collation_server
#   * foreign_key_checks
#   * identity
#   * last_insert_id
#   * lc_time_names
#   * pseudo_thread_id
#   * sql_auto_is_null
#   * time_zone
#   * timestamp
#     --> "shake_clock" affects timestamp
#   * unique_checks
#   For information about how replication treats sql_mode, see Section 16.3.1.30, “Replication and Variables”.
#   * When one of the tables involved is a log table in the mysql database.
#   * When the LOAD_FILE() function is used. (Bug#39701)
#   --> "value_unsafe_for_sbr"
#-----------------------------
# When using statement-based replication, the LOAD DATA INFILE statement's CONCURRENT  option is not replicated;
# that is, LOAD DATA CONCURRENT INFILE is replicated as LOAD DATA INFILE, and LOAD DATA CONCURRENT LOCAL INFILE
# is replicated as LOAD DATA LOCAL INFILE. The CONCURRENT option is replicated when using row-based replication. (Bug#34628)
#   --> Use of "concurrent_or_empty" in "dml"
#-------------------------------
# If you have databases on the master with character sets that differ from the global character_set_server value, you should
# design your CREATE TABLE statements so that tables in those databases do not implicitly rely on the database default character set.
# A good workaround is to state the character set and collation explicitly in CREATE TABLE statements.
#-----------------------------------
# MySQL 5.4.3 and later.   Every CREATE DATABASE IF NOT EXISTS statement is replicated, whether or not the database already exists on
# the master. Similarly, every CREATE TABLE IF NOT EXISTS statement is replicated, whether or not the table already exists on the master.
# This includes CREATE TABLE IF NOT EXISTS ... LIKE. However, replication of CREATE TABLE IF NOT EXISTS ... SELECT follows somewhat
# different rules; see Section 16.3.1.4, “Replication of CREATE TABLE ... SELECT Statements”, for more information.
#
# Replication of CREATE EVENT IF NOT EXISTS.  CREATE EVENT IF NOT EXISTS is always replicated in MySQL 5.4, whether or not the event
# named in this statement already exists on the master.  See also Bug#45574.
#-----------------------------------
# http://dev.mysql.com/doc/refman/5.4/en/replication-features-differing-tables.html
#-----------------------------------
# http://dev.mysql.com/doc/refman/5.4/en/replication-features-floatvalues.html
#-----------------------------------
# http://dev.mysql.com/doc/refman/5.4/en/replication-features-flush.html
#-----------------------------------
# USE LIMIT WITH ORDER BY    safety_check needs to be switched off otherwise we get a false alarm
#-----------------------------------
# http://dev.mysql.com/doc/refman/5.4/en/replication-features-slaveerrors.html
# FOREIGN KEY, master InnoDB and slave MyISAM
#-----------------------------------
# http://dev.mysql.com/doc/refman/5.4/en/replication-features-max-allowed-packet.html
# BLOB/TEXT value too big for max-allowed-packet on master or on slave
#-----------------------------------
# http://dev.mysql.com/doc/refman/5.4/en/replication-features-timeout.html
# Slave: Innodb detects deadlock -> slave_transaction_retries to run the action to replicate ....
# mleich: Most probably not doable with current RQG
#-----------------------------------
# The same system time zone should be set for both master and slave. If not -> problems with NOW() or FROM_UNIXTIME()
# CONVERT_TZ(...,...,@@session.time_zone)  is properly replicated ...
#-----------------------------------
# In situations where transactions mix updates to transactional and nontransactional tables, the order of statements
# in the binary log is correct, and all needed statements are written to the binary log even in case of a ROLLBACK.
# However, when a second connection updates the nontransactional table before the first connection's transaction is
# complete, statements can be logged out of order, because the second connection's update is written immediately after
# it is performed, regardless of the state of the transaction being performed by the first connection.
#    --> grammar items trans_table , nontrans_table, + use only one sort of table within transaction
#    --> LOCK ALL TABLES + runs transaction with both sorts of tables   ???
#----------------------------------------------------------------------------------------------------------------------
# Due to the nontransactional nature of MyISAM  tables, it is possible to have a statement that only partially updates
# a table and returns an error code. This can happen, for example, on a multiple-row insert that has one row violating
# a key constraint, or if a long update statement is killed after updating some of the rows.
# If that happens on the master, the slave thread exits and waits for the database administrator to decide what to do
# about it unless the error code is legitimate and execution of the statement results in the same error code on the slave.
#-----------------------------------
# When the storage engine type of the slave is nontransactional, transactions on the master that mix updates of transactional
# and nontransactional tables should be avoided because they can cause inconsistency of the data between the master's
# transactional table and the slave's nontransactional table.
#-----------------------------------
# http://dev.mysql.com/doc/refman/5.4/en/replication-features-triggers.html !!!!
#-----------------------------------
# TRUNCATE is treated for purposes of logging and replication as DDL rather than DML ...
# --> later
#-----------------------------------
# http://dev.mysql.com/doc/refman/5.4/en/mysqlbinlog-hexdump.html
#    Type 	Name 	Meaning
#    00 	UNKNOWN_EVENT 	This event should never be present in the log.
#    01 	START_EVENT_V3 	This indicates the start of a log file written by MySQL 4 or earlier.
# X  02 	QUERY_EVENT 	The most common type of events. These contain statements executed on the master.
# ?  03 	STOP_EVENT 	Indicates that master has stopped.
# X  04 	ROTATE_EVENT 	Written when the master switches to a new log file.
#        --> "rotate_event"
# X  05 	INTVAR_EVENT 	Used for AUTO_INCREMENT values or when the LAST_INSERT_ID() function is used in the statement.
#        --> "value" contains NULL and (nested) LAST_INSERT_ID()
#    06 	LOAD_EVENT 	Used for LOAD DATA INFILE in MySQL 3.23.
#    07 	SLAVE_EVENT 	Reserved for future use.
#    08 	CREATE_FILE_EVENT 	Used for LOAD DATA INFILE statements. This indicates the start of execution of such a statement. A temporary file is created on the slave. Used in MySQL 4 only.
# X  09 	APPEND_BLOCK_EVENT 	Contains data for use in a LOAD DATA INFILE statement. The data is stored in the temporary file on the slave.
#        --> "dml" contains LOAD DATA
#    0a 	EXEC_LOAD_EVENT 	Used for LOAD DATA INFILE statements. The contents of the temporary file is stored in the table on the slave. Used in MySQL 4 only.
# X  0b 	DELETE_FILE_EVENT 	Rollback of a LOAD DATA INFILE statement. The temporary file should be deleted on the slave.
#        --> "dml" contains LOAD DATA
#    0c 	NEW_LOAD_EVENT 	Used for LOAD DATA INFILE in MySQL 4 and earlier.
# X  0d 	RAND_EVENT 	Used to send information about random values if the RAND() function is used in the statement.
#        --> "value_rand"
# X  0e 	USER_VAR_EVENT 	Used to replicate user variables.
#        --> "dml" containing SET @aux + "values" containg @aux
# X  0f 	FORMAT_DESCRIPTION_EVENT 	This indicates the start of a log file written by MySQL 5 or later.
#        --> ?
# X  10 	XID_EVENT 	Event indicating commit of an XA transaction.
# X  11 	BEGIN_LOAD_QUERY_EVENT 	Used for LOAD DATA INFILE statements in MySQL 5 and later.
# X  12 	EXECUTE_LOAD_QUERY_EVENT 	Used for LOAD DATA INFILE statements in MySQL 5 and later.
#        --> "dml" contains LOAD DATA
# X  13 	TABLE_MAP_EVENT 	Information about a table definition. Used in MySQL 5.1.5 and later.
#    14 	PRE_GA_WRITE_ROWS_EVENT 	Row data for a single table that should be created. Used in MySQL 5.1.5 to 5.1.17.
#    15 	PRE_GA_UPDATE_ROWS_EVENT 	Row data for a single table that needs to be updated. Used in MySQL 5.1.5 to 5.1.17.
#    16 	PRE_GA_DELETE_ROWS_EVENT 	Row data for a single table that should be deleted. Used in MySQL 5.1.5 to 5.1.17.
# X  17 	WRITE_ROWS_EVENT 	Row data for a single table that should be created. Used in MySQL 5.1.18 and later.
#        --> insert
# X  18 	UPDATE_ROWS_EVENT 	Row data for a single table that needs to be updated. Used in MySQL 5.1.18 and later.
#        --> update
# X  19 	DELETE_ROWS_EVENT 	Row data for a single table that should be deleted. Used in MySQL 5.1.18 and later.
#        --> delete
# 1a 	INCIDENT_EVENT 	Something out of the ordinary happened. Added in MySQL 5.1.18.
# My (mleich) markings:
# X needs sub test
# I most probably already covered (FIXME: Check in hex dump)
#------------------------------------------------
# The following restriction applies to statement-based replication only, not to row-based replication.
# The GET_LOCK(), RELEASE_LOCK(), IS_FREE_LOCK(), and IS_USED_LOCK() functions that handle user-level locks are replicated
# without the slave knowing the concurrency context on master. Therefore, these functions should not be used to insert
# into a master's table because the content on the slave would differ.
# (For example, do not issue a statement such as INSERT INTO mytable VALUES(GET_LOCK(...)).)
#------------------------------------------------
# http://dev.mysql.com/doc/refman/5.4/en/replication-features-functions.html
# The USER(), CURRENT_USER(), UUID(), VERSION(), and LOAD_FILE() functions are replicated without change and thus do not
# work reliably on the slave unless row-based replication is enabled. This is also true for CURRENT_USER. (See Section 16.1.2, “Replication Formats”.)
#
# USER(), CURRENT_USER(), and CURRENT_USER are automatically replicated using row-based replication when using MIXED mode, and generate a warning in STATEMENT mode. (Bug#28086)
# the SYSDATE() function is not replication-safe
# FOUND_ROWS() and ROW_COUNT() functions are not replicated reliably using statement-based + generate a warning in STATEMENT mode
# --> "values"

#################################################
# From the discussion:
# - If you want to change the replication format, do so outside the boundaries of a transaction. (SBR?)
#   --> "*_dml_event"
# - In statement based replication, any non-transactional statement should be either placed outside the boundaries of a transaction or before any transactional statement.
#   note(mleich): transactional/non-transactional statement refers to the table/tables where something is modified
#------------------------------------------------
#################################################
# Experience with mysql-5.1-rep+3 with BINLOG_FORMAT = STATEMENT
# 1. SAVEPOINT A followed by some UPDATE on a myisam table
# 2. Mixup of transactional and nontransactional table in any table modifying statement
#    (independend of the use - modify or just query - of the table)
# 3. Statement with nontransactional table occurs after statement with transactional table
# end up with
# Note 1592	Unsafe statement binlogged in statement format since BINLOG_FORMAT = STATEMENT.
#           Reason for unsafeness: Non-transactional reads or writes are unsafe if they occur
#           after transactional reads or writes inside a transaction.
#

safety_check:
	# For debugging the grammar use
	{ return '/*' . $pick_mode . '*/' } /* QUERY_IS_REPLICATION_SAFE */ ;
	# For faster execution set this grammar element to "empty".
	# ;

query:
	binlog_event ;

query_init:
	# We need to set "$pick_mode = 3" because of the following possible scenario
	# Current GLOBAL BINLOG_FORMAT is probably STATEMENT.
	# --> connect and initial SESSION BINLOG_FORMAT equals GLOBAL BINLOG_FORMAT (STATEMENT)
	# --> query -> binlog_event -> dml_event -> binlog_format_set ->
	# --> rand_global_binlog_format (this does not set $pick_mode and has no impact
	#     on our SESSION BINLOG_FORMAT) --> dml_list --> dml
	# $pick_mode cannot "help" during statement generation in "dml". So it could happen
	# that we get a statement using a transactional and a non transactional table.
	# And this is UNSAFE if our current SESSION BINLOG_FORMAT is STATEMENT.
	# $pick_mode = 3 brings us to the safe side.
	{ $pick_mode = 3 ; return undef } ;

binlog_event:
	/* BEGIN 1 */ dml_event    /* 1 END */  |
	/* BEGIN 1 */ dml_event    /* 1 END */  |
	/* BEGIN 1 */ dml_event    /* 1 END */  |
	/* BEGIN 1 */ dml_event    /* 1 END */  |
	/* BEGIN 1 */ dml_event    /* 1 END */  |
	/* BEGIN 1 */ dml_event    /* 1 END */  |
	set_iso_level      |
	set_iso_level      |
	rotate_event       ;
	# This fools the RQG deadlock detection shake_clock        ;

set_iso_level:
	safety_check SET global_or_session TRANSACTION ISOLATION LEVEL iso_level ;
iso_level:
	{ if ( $format == 'STATEMENT' ) { return $prng->arrayElement(['REPEATABLE READ','SERIALIZABLE']) } else { return $prng->arrayElement(['READ UNCOMMITTED','READ COMMITTED','REPEATABLE READ','SERIALIZABLE']) } } ;

global_or_session:
	SESSION | GLOBAL ;

shake_clock:
	safety_check SET SESSION TIMESTAMP = UNIX_TIMESTAMP() plus_minus _digit ;

rotate_event:
	# Cause that the master switches to a new binary log file
	# RESET MASTER is not useful here because it causes
	# - master.err: [ERROR] Failed to open log (file '/dev/shm/var_290/log/master-bin.000002', errno 2)
	# - the RQG test does not terminate in usual way (RQG assumes deadlock)
	safety_check FLUSH LOGS ;

xid_event:
	# Omit BEGIN because it is only an alias for START TRANSACTION
	START TRANSACTION                     |
	COMMIT                                |
	ROLLBACK                              |
	# In SBR after a "SAVEPOINT A" any statement which modifies a nontransactional table is unsafe.
	# Therefore we enforce here that future statements within the current transaction use
	# a transactional table.
	SAVEPOINT A { $pick_mode=3; return undef} |
	ROLLBACK TO SAVEPOINT A               |
	RELEASE SAVEPOINT A                   |
	implicit_commit                       ;

implicit_commit:
	# mleich: A CREATE/ALTER TABLE which fails because the object already exists/does not exist causes an
	#         implicite COMMIT before the "core" of the statement is executed.
	#         RPL has problems with concurrent DDL but is this also valid for such cases?
	CREATE TABLE mysql.user ( f1 BIGINT )    |
	ALTER TABLE does_not_exist CHANGE COLUMN f1 f2 BIGINT |
	# CREATE DATABASE ic ; CREATE TABLE ic . _letter SELECT * FROM _table LIMIT digit ; DROP DATABASE ic |
	# CREATE USER _letter | DROP USER _letter | RENAME USER _letter TO _letter |
	SET AUTOCOMMIT = ON |
	SET AUTOCOMMIT = OFF |
	# CREATE TABLE IF NOT EXISTS _letter ENGINE = engine SELECT * FROM _table LIMIT digit |
	# RENAME TABLE _letter TO _letter |
	# TRUNCATE TABLE _letter |
	# DROP TABLE IF EXISTS _letter |
	# Grant/Revoke
	# FLUSH
	# LOAD DATA INFILE causes an implicit commit only for tables using the NDB storage engine
	LOCK TABLE _table WRITE ; safety_check UNLOCK TABLES ;

# Guarantee that the transaction has ended before we switch the binlog format
dml_event:
	COMMIT ; safety_check binlog_format_set ; dml_list ; safety_check xid_event ;
dml_list:
	safety_check dml |
	safety_check dml nontrans_trans ; dml_list ;

nontrans_trans:
	# This is needed for the generation of the following scenario.
	# m statements of an transaction use non transactional tables followed by
	# n statements which use transactional tables.
	{ if ( ($prng->int(1,4) == 4) && ($pick_mode == 4) ) { $pick_mode = 3 } ; return undef } ;

binlog_format_set:
	# 1. SESSION BINLOG_FORMAT --> How the actions of our current session will be bin logged
	# 2. GLOBAL BINLOG_FORMAT  --> How actions with DELAYED will be bin logged
	#                          --> Initial SESSION BINLOG_FORMAT of session started in future
	# This means any SET GLOBAL BINLOG_FORMAT ... executed by any session has no impact on any
	# already existing session (except 2.).
	rand_global_binlog_format  |
	rand_session_binlog_format |
	rand_session_binlog_format |
	rand_session_binlog_format ;
rand_global_binlog_format:
	SET GLOBAL BINLOG_FORMAT = STATEMENT |
	SET GLOBAL BINLOG_FORMAT = MIXED     |
	SET GLOBAL BINLOG_FORMAT = ROW       ;
rand_session_binlog_format:
	SET SESSION BINLOG_FORMAT = { $format = 'STATEMENT' ; $pick_mode = $prng->int(1,4) ; return $format } |
	SET SESSION BINLOG_FORMAT = { $format = 'MIXED'     ; $pick_mode = 0               ; return $format } |
	SET SESSION BINLOG_FORMAT = { $format = 'ROW'       ; $pick_mode = 0               ; return $format } ;

dml:
	# Enable the next line if
	#    Bug#49628 corrupt table after legal SQL, LONGTEXT column
	# is fixed.
	# generate_outfile ; safety_check LOAD DATA concurrent_or_empty INFILE _tmpnam REPLACE INTO TABLE pick_schema pick_safe_table |
	# We MUST reduce the huge amount of NULL's
	UPDATE ignore pick_schema pick_safe_table SET _field[invariant] = col_tinyint WHERE col_tinyint BETWEEN _tinyint[invariant] AND _tinyint[invariant] + _digit AND _field[invariant] IS NULL |
	UPDATE ignore pick_schema pick_safe_table SET _field[invariant] = col_tinyint WHERE col_tinyint BETWEEN _tinyint[invariant] AND _tinyint[invariant] + _digit AND _field[invariant] IS NULL |
	update |
	delete |
	insert |
	# LOAD DATA INFILE ... is not supported in prepared statement mode.
	PREPARE st1 FROM " update " ; safety_check EXECUTE st1 ; DEALLOCATE PREPARE st1 |
	PREPARE st1 FROM " delete " ; safety_check EXECUTE st1 ; DEALLOCATE PREPARE st1 |
	PREPARE st1 FROM " insert " ; safety_check EXECUTE st1 ; DEALLOCATE PREPARE st1 |
	# We need the next statement for other statements which should use a user variable.
	# As long as
	#    Bug#49562 SBR out of sync when using numeric data types + user variable
	# is bot fixed we must prevent that a value assigned to @aux does not exceed
	# the value range of the column where it is applied (INSERT/UPDATE) later.
	# SET @aux = value  |
	SET @aux = 13  |
	# We need the next statements for other statements which should be affected by switching the database.
	USE `test` | USE `test1` |
	select_for_update |
	xid_event         ;

generate_outfile:
	SELECT * FROM pick_schema pick_safe_table ORDER BY _field INTO OUTFILE _tmpnam ;
concurrent_or_empty:
	| CONCURRENT ;

pick_schema:
	|
	test .  |
	test1 . ;

delete:
	# Delete in one table, search in one table
	# Unsafe in statement based replication except we add ORDER BY
	# DELETE       FROM pick_schema _table            LIMIT 1   |
	DELETE low_priority quick ignore       FROM pick_schema pick_safe_table               where    |
	# Delete in two tables, search in two tables
	# Note: Unfortunately next grammar line leads to frequent failing statements (Unknown table A or B)
	#       The reason is that in case both tables are located in different SCHEMA's than the
	#       the schema_name must be written before the table alias.
	#       Example: DELETE test.A, test1.B FROM test.t1 AS A NATURAL JOIN test1.t7 AS B ....
	#  DELETE low_priority quick ignore A , B FROM pick_schema pick_safe_table AS A join     where    |
	DELETE low_priority quick ignore A , B FROM pick_safe_table AS A NATURAL JOIN pick_safe_table B where |
	DELETE low_priority quick ignore test1.A , test.B FROM test1 . pick_safe_table AS A NATURAL JOIN test . pick_safe_table B where ;

join:
	# 1. Do not place a where condition here.
	# 2. join is also use when modifying two tables in one statement.
	#    Therefore we must use "pick_safe_table" here.
	NATURAL JOIN pick_schema pick_safe_table B ;
subquery:
	correlated | non_correlated ;
subquery_part1:
	AND A. _field[invariant] IN ( SELECT _field[invariant] FROM pick_schema pick_safe_table AS B ;
correlated:
	subquery_part1 WHERE B.col_tinyint = A.col_tinyint ) ;
non_correlated:
	subquery_part1 ) ;
where:
	WHERE col_tinyint BETWEEN _tinyint[invariant] AND _tinyint[invariant] + 2 ;

insert:
	# Insert into one table, search in no other table
	INSERT low_priority_delayed_high_priority ignore INTO pick_schema pick_safe_table ( _field , col_tinyint )   VALUES values_list on_duplicate_key_update |
	# Insert into one table, search in >= 1 tables
	INSERT low_priority_delayed_high_priority ignore INTO pick_schema pick_safe_table ( _field_list[invariant] ) SELECT _field_list[invariant] FROM table_in_select AS A addition ;

values_list:
	( value , _tinyint ) |
	( value , _tinyint ) , ( value , _tinyint ) ;

on_duplicate_key_update:
	# Only 10 %
	| | | | | | | | |
	ON DUPLICATE KEY UPDATE _field = value ;

table_in_select:
	pick_schema pick_safe_table                                        |
	( SELECT _field_list[invariant] FROM pick_schema pick_safe_table ) ;

addition:
	where | where subquery | join where | where union where ;

union:
	UNION SELECT _field_list[invariant] FROM table_in_select AS B ;

replace:
	# HIGH_PRIORITY is not allowed
	REPLACE low_priority_delayed INTO pick_schema pick_safe_table ( _field , col_tinyint )   VALUES values_list on_duplicate_key_update |
	REPLACE low_priority_delayed INTO pick_schema pick_safe_table ( _field_list[invariant] ) SELECT _field_list[invariant] FROM table_in_select AS A addition ;

update:
	# mleich: Search within another table etc. should be already sufficient covered by "delete" and "insert".
	# Update one table
	UPDATE ignore pick_schema pick_safe_table SET _field = value where |
	# Update two tables
	UPDATE ignore pick_schema pick_safe_table AS A join SET A. _field = value , B. _field = value where ;

select_for_update:
	# SELECT does not get replicated, but we want its sideeffects on the transaction.
	# FIXME (mleich): 1. If _field picks the blob, do we have a bad impact on throughput?
	#                 2. Has a column list sideeffects on the transaction at all
	#                    compared to SELECT 1 FROM ....?
	SELECT col_tinyint, _field FROM pick_safe_table where FOR UPDATE;

value:
	value_numeric          |
	value_rand             |
	value_string_converted |
	value_string           |
	value_temporal         |
	@aux                   |
	NULL                   |
	{ if ($format=='STATEMENT') {return '/*'} } value_unsafe_for_sbr { if ($format=='STATEMENT') {return '*/ 17 '} };

value_unsafe_for_sbr:
# Functions which are unsafe when bin log format = 'STATEMENT'
# + we get a warning : "Statement may not be safe to log in statement format"
	# bigint(21)
	FOUND_ROWS()      |
	ROW_COUNT()       |
	# varchar(36) CHARACTER SET utf8
	UUID()            |
	# bigint(21) unsigned
	UUID_SHORT()      |
	# varchar(77) CHARACTER SET utf8
	CURRENT_USER()    |
	USER()            |
	VERSION()         |
	SYSDATE()         |
	# _data gets replace by LOAD_FILE( <some path> ) which is unsafe for SBR.
	# mleich: I assume this refers to the risk that an input file
	#         might exist on the master but probably not on the slave.
	#         This is irrelevant for the usual RQG test configuration
	#         where master and slave run on the same box.
	_data             ;

value_numeric:
	# We have 'bit' -> bit(1),'bit(4)','bit(64)','tinyint','smallint','mediumint','int','bigint',
	# 'float','double',
	# 'decimal' -> decimal(10,0),'decimal(35)'
	# mleich: FIXME 1. We do not need all of these values.
	#               2. But a smart distribution of values is required so that we do not hit all time
	#                  outside of the allowed value ranges
	- _digit         | _digit              |
	_bit(1)          | _bit(4)             |
	_tinyint         | _tinyint_unsigned   |
	_smallint        | _smallint_unsigned  |
	_mediumint       | _mediumint_unsigned |
	_int             | _int_unsigned       |
	_bigint          | _bigint_unsigned    |
	_bigint          | _bigint_unsigned    |
	-2.0E-1          | +2.0E-1             |
	-2.0E+1          | +2.0E+1             |
	-2.0E-10         | +2.0E-10            |
	-2.0E+10         | +2.0E+10            |
	-2.0E-100        | +2.0E-100           |
	-2.0E+100        | +2.0E+100           |
	# int(10)
	CONNECTION_ID()  |
	# Value of the AUTOINCREMENT (per manual only applicable to integer and floating-point types)
	# column for the last INSERT.
	LAST_INSERT_ID() ;

value_rand:
	# The ( _digit ) makes thread = 1 tests deterministic.
	RAND( _digit )   ;

value_string:
	# We have 'char' -> char(1),'char(10)',
	# 'varchar' - varchar(1),'varchar(10)','varchar(257)',
	# 'tinytext','text','mediumtext','longtext',
	# 'enum', 'set'
	# mleich: I fear values > 16 MB are risky, so I omit them.
	_char(1)    | _char(10)    |
	_varchar(1) | _varchar(10) | _varchar(257)   |
	_text(255)  | _text(65535) | _text(16777215) |
	DATABASE()  |
	_set        ;

value_string_converted:
	CONVERT( value_string USING character_set );

character_set:
	UTF8 | UCS2 | LATIN1 | BINARY ;

value_temporal:
	# We have 'datetime', 'date', 'timestamp', 'time','year'
	#    _datetime - a date+time value in the ISO format 2000-01-01 00:00:00
	#    _date - a valid date in the range from 2000 to 2010
	#    _timestamp - a date+time value in the MySQL format 20000101000000
	#    _time - a time in the range from 00:00:00 to 29:59:59
	#    _year - a year in the range 2000 to 2010
	_datetime | _date | _time | _datetime | _timestamp | _year |
	NOW()     ;

any_table:
	undef_table    |
	nontrans_table |
	trans_table    ;

undef_table:
	table0_int_autoinc  |
	table0_int          |
	table0              |
	table1_int_autoinc  |
	table1_int          |
	table1              |
	table10_int_autoinc |
	table10_int         |
	table10             ;

nontrans_table:
	table0_myisam_int_autoinc  |
	table0_myisam_int          |
	table0_myisam              |
	table1_myisam_int_autoinc  |
	table1_myisam_int          |
	table1_myisam              |
	table10_myisam_int_autoinc |
	table10_myisam_int         |
	table10_myisam             ;

trans_table:
	table0_innodb_int_autoinc  |
	table0_innodb_int          |
	table0_innodb              |
	table1_innodb_int_autoinc  |
	table1_innodb_int          |
	table1_innodb              |
	table10_innodb_int_autoinc |
	table10_innodb_int         |
	table10_innodb             ;

pick_safe_table:
	# pick_mode | table type to choose | setting
	# 0         | any                                  any_table    /*          undef_table                nontrans_table                trans_table    */
	# 1         | undef                    /*          any_table    */          undef_table    /*          nontrans_table                trans_table    */
	# 2         | nontrans                 /*          any_table                undef_table    */          nontrans_table    /*          trans_table    */
	# 3         | trans                    /*          any_table                undef_table                nontrans_table    */          trans_table
	# 4         | nontrans                 /*          any_table                undef_table    */          nontrans_table    /*          trans_table    */
	tmarker_init tmarker_set            { return $m0 } any_table { return $m1 } undef_table { return $m2 } nontrans_table { return $m3 } trans_table { return $m4 } ;

tmarker_init:
	{ $m0 = ''; $m1 = ''; $m2 = ''; $m3 = ''; $m4 = ''; return undef } ;

tmarker_set:
	{ if ($pick_mode==0) {$m1='/*';$m4='*/'} elsif ($pick_mode==1) {$m0='/*';$m1='*/';$m2='/*';$m4='*/'} elsif ($pick_mode==2) {$m0='/*';$m2='*/';$m3='/*';$m4='*/'} elsif ($pick_mode==3) {$m0='/*';$m3='*/'} elsif ($pick_mode==4) {$m0='/*';$m2='*/';$m3='/*';$m4='*/'} ; return undef };


#### Basic constructs which are used at various places

delayed:
	# "DELAYED" is declared to be unsafe whenever the GLOBAL binlog_format is 'statement'.
	# --> Either
	#     - set GLOBAL binlog_format during query_init, don't switch it later and adjust usage of delayed ?
	#     or
	#     - do not use DELAYED (my choice, mleich)
	# DELAYED       |
	;

high_priority:
	|
	HIGH_PRIORITY ;

ignore:
	# Only 10 %
	| | | | | | | | |
	# mleich temporary disabled IGNORE ;
	;

low_priority:
	| | |
	LOW_PRIORITY ;

low_priority_delayed_high_priority:
# All MyISAM only features.
	| |
	low_priority  |
	delayed       |
	high_priority ;

low_priority_delayed:
	| |
	low_priority |
	delayed      ;

plus_minus:
	+ | - ;

quick:
	# Only 10 %
	| | | | | | | | |
	QUICK ;
