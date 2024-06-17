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
# 1. Use this grammar (percona_qa.yy) in combination with percona_qa.zz & percona_qa.cc
# 2. Use a duration of 300 to 900 seconds. Short durations ensure fresh/plenty data in the tables
# 3. For use with combinations.pl (assuming a high-end testing server):
#    - 10 RQG threads (--parallel=10) with one SQL thread (--threads=1) for single threaded testing
#    - 8 RQG threads (--parallel=8) with 10-30 SQL threads (--threads=10) for multi threaded testing
#    - Both these over many (400+) trials, both in normal and Valgrind mode, to catch most issues
# 4. You can use --short_column_names option to RQG to avoid overly long column names
# 5. Do not use the --engines option, storage engine assignent is done in percona_qa.zz

# TODO:
# Find a working solution for these types of rules:
#	"log_slow_filter_list,log_slow_filter_list" ; 
#	"log_slow_verbosity_list,log_slow_verbosity_list";
#	"slow_query_log_use_global_control_list,slow_query_log_use_global_control_list" ;
# As they are, they fail, may want to try spaces; " a , a " 
# Also, PURGE ARCHIVED LOGS TO cannot be added due to not having actual filename.

# Temp workaround: i_s |            removed from query: to avoid I_S assertions ftm. Assertions look like this:
#                                   "Assertion `!table || !table->in_use || table->in_use == _current_thd()'"
#                                   (IMPORTANT: to be re-tested later for tokudb+i_s interoperability)
#                                   Ref https://bugs.launchpad.net/percona-server/+bug/1260152
#   - RV 23/9/14: re-instated i_s but excluded i-s-temp tables due to https://bugs.launchpad.net/percona-server/+bug/1367922
#   - If we observe https://bugs.launchpad.net/percona-server/+bug/1260152 again now, then that bug is not i-s-temp related (update!)
# Temp workaround: fake_changes |   removed from query: due to Percona feature WIP
# Temp workaround: i_s_toku |	    removed from query:  These statements cause 'void Protocol::end_statement' 
#				    asserts - Sig6 void Protocol::end_statement(): Assertion `0' failed. - Logged with TokuTek 28/08/14
#				    (Also remember to re-instate i_s_toku in redef file)

# INSTALL PLUGIN tokudb SONAME 'ha_tokudb.so'; is instead handled by a mysqld option in the cc file: 
# --mysqld=--plugin-load=tokudb=ha_tokudb.so - This is to ensure that TokuDB engine is available to
# enable using --tokudb=... options from the .cc file. The rest of the TokuDB plugin parts (all I_S type)
# are loaded using the --mysqld=--init-file=... option as listed in the cc file.
#
# To check if all modules are loaded use SHOW PLUGINS; 
# mysql> SHOW PLUGINS;
# | Name                          | Status   | Type               | Library      | License |
# [...]
# | TokuDB                        | ACTIVE   | STORAGE ENGINE     | ha_tokudb.so | GPL     | # Loaded by --plugin-load=...
# | TokuDB_file_map               | ACTIVE   | INFORMATION SCHEMA | ha_tokudb.so | GPL     | # Loaded by --init-file=...
# | TokuDB_fractal_tree_info      | ACTIVE   | INFORMATION SCHEMA | ha_tokudb.so | GPL     | # Idem, etc.
# | TokuDB_fractal_tree_block_map | ACTIVE   | INFORMATION SCHEMA | ha_tokudb.so | GPL     |
# | TokuDB_trx                    | ACTIVE   | INFORMATION SCHEMA | ha_tokudb.so | GPL     |
# | TokuDB_locks                  | ACTIVE   | INFORMATION SCHEMA | ha_tokudb.so | GPL     |
# | TokuDB_lock_waits             | ACTIVE   | INFORMATION SCHEMA | ha_tokudb.so | GPL     |

query:
	select | select | insert | insert | delete | delete | replace | update | transaction |
	alter | views | set | flush | proc_func | outfile_infile | update_multi | kill_idle | query_cache |
	ext_slow_query_log | user_stats | drop_create_table | table_comp | table_comp | optimize_table | 
	bitmap | bitmap | archive_logs | thread_pool | max_stmt_time | locking | prio_shed |
	cleaner | preflush | toku_clustering_key | toku_clustering_key | audit_plugin | binlog_event | 
	i_s_buffer_pool_stats | full_text_index | i_s | i_s_fti ;

zero_to_ten:
	0 | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 ;

one_to_ten:
	1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 ;

three_to_twenty:
	3 | 5 | 7 | 9 | 10 | 11 | 13 | 15 | 17 | 19 | 20 ;

zero_to_forty:
	0 | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
	11 | 12 | 13 | 14 | 15 | 16 | 17 | 18 | 19 | 20 |
	21 | 22 | 23 | 24 | 25 | 26 | 27 | 28 | 29 | 30 |
	31 | 32 | 33 | 34 | 35 | 36 | 37 | 38 | 39 | 40 ;

zero_to_thousand:
	0 | 1 | 2 | 10 | 100 | 150 | 200 | 250 | 300 | 400 | 500 | 600 | 650 | 700 | 800 | 900 | 999 | 1000 ;

zero_to_ttsh:
	0 | 1 | 2 | 10 | 100 | 200 | 450 | 750 | 1111 | 1000 | 1202 | 1500 | 1700 | 2000 | 2400 | 2600 | 3000 | 3300 | 3600 ;

hundred_to_thousand:
	100 | 150 | 200 | 250 | 300 | 400 | 500 | 600 | 650 | 700 | 800 | 900 | 999 | 1000 ;

thousand_to_tts:
	1000 | 2000 | 2500 | 5000 | 7500 | 10000 ;

# Post 5.6 GA, add/try shorter msec durations to max_stmt_time_range also
max_stmt_time_range:
	100000 | 200000 | 300000 ;

prio_shed:
	SELECT @@GLOBAL.innodb_sched_priority_cleaner |
	SET GLOBAL innodb_sched_priority_cleaner = zero_to_forty ;

cleaner:
	SET GLOBAL innodb_cleaner_lsn_age_factor = 'legacy' |
	SET GLOBAL innodb_cleaner_lsn_age_factor = 'high_checkpoint' ;

preflush:
	SELECT @@GLOBAL.innodb_foreground_preflush |
	SET GLOBAL innodb_foreground_preflush = 'sync_preflush' |
	SET GLOBAL innodb_foreground_preflush = 'exponential_backoff' ;

max_stmt_time:
	SET scope MAX_STATEMENT_TIME = max_stmt_time_range |
	SHOW scope VARIABLES LIKE 'HAVE_STATEMENT_TIMEOUT' |
	SHOW scope STATUS LIKE 'MAX_STATEMENT_TIME_EXCEEDED' |
	SHOW scope STATUS LIKE 'MAX_STATEMENT_TIME_SET' |
	SHOW scope STATUS LIKE 'MAX_STATEMENT_TIME_SET_FAILED' ;

toku_clustering_key:
	CREATE TABLE if_not_exists tck_or_other ( c1 type null_or_not , c2 type default_or_not , c3 type , c4 type null_or_not default_or_not , tb_keydef, tck_keydef ) ENGINE = engine ROW_FORMAT = row_format KEY_BLOCK_SIZE = kb_size |
	CREATE TABLE if_not_exists tck_or_other ( c1 type null_or_not , c2 type default_or_not , c3 type , c4 type null_or_not default_or_not , tb_keydef , tck_keydef ) ENGINE = engine |
	CREATE TABLE if_not_exists tck_or_other ( c1 type null_or_not , c2 type default_or_not , c3 type null_or_not default_or_not , tb_keydef , tck_keydef ) ENGINE = engine |
	CREATE TABLE if_not_exists tck_or_other ( c1 type null_or_not , c2 type default_or_not , c3 type , c4 type null_or_not default_or_not , tck_keydef ) ENGINE = engine ROW_FORMAT = row_format KEY_BLOCK_SIZE = kb_size |
	CREATE TABLE if_not_exists tck_or_other ( c1 type null_or_not , c2 type default_or_not , c3 type , c4 type null_or_not default_or_not , tck_keydef ) ENGINE = engine |
	CREATE TABLE if_not_exists tck_or_other ( c1 type null_or_not , c2 type default_or_not , c3 type null_or_not default_or_not , tck_keydef ) ENGINE = engine |
	CREATE TABLE if_not_exists tck_or_other ( c1 type null_or_not tck_or_not , c2 type default_or_not tck_or_not , c3 type tck_or_not , c4 type null_or_not default_or_not tck_or_not , tck_keydef ) ENGINE = engine ROW_FORMAT = row_format KEY_BLOCK_SIZE = kb_size |
	CREATE TABLE if_not_exists tck_or_other ( c1 type null_or_not tck_or_not , c2 type default_or_not tck_or_not , c3 type tck_or_not , c4 type null_or_not default_or_not tck_or_not , tck_keydef ) ENGINE = engine |
	CREATE TABLE if_not_exists tck_or_other ( c1 type null_or_not tck_or_not , c2 type default_or_not tck_or_not , c3 type tck_or_not , c4 type null_or_not default_or_not tck_or_not ) ENGINE = engine |
	CREATE TABLE if_not_exists tck_or_other ( c1 type null_or_not tck_or_not , c2 type default_or_not tck_or_not , c3 type null_or_not default_or_not tck_or_not , tck_keydef ) ENGINE = engine |
	ALTER TABLE tck_or_other ADD tck_keydef | ALTER TABLE tck_or_other ADD tck_keydef | ALTER TABLE tck_or_other ADD tck_keydef |
	ALTER TABLE tck_or_other ADD tck_keydef | ALTER TABLE tck_or_other ADD tck_keydef | ALTER TABLE tck_or_other ADD tck_keydef |
	CREATE CLUSTERING INDEX k2 ON tck_or_other ( tck_field_list ) |
	CREATE tck_or_not k2 ON tck_or_other ( tck_field_list ) |
	DROP TABLE tb_toku_clust_key | DROP TABLE tb_toku_clust_key | DROP TABLE tb_toku_clust_key | DROP TABLE tb_toku_clust_key |
	INSERT INTO tb_toku_clust_key VALUES ( value , value , value , value ) |
	INSERT INTO tb_toku_clust_key VALUES ( value , value , value , value ) |
	INSERT INTO tb_toku_clust_key VALUES ( value , value , value ) |
	ALTER TABLE tb_toku_clust_key ROW_FORMAT = row_format |
	ALTER TABLE tb_toku_clust_key ROW_FORMAT = row_format KEY_BLOCK_SIZE = kb_size |
	ALTER TABLE tb_toku_clust_key KEY_BLOCK_SIZE = kb_size |
	ALTER TABLE tb_toku_clust_key DROP PRIMARY key_or_index ;

tck_keydef:
	CLUSTERING KEY k1 ( tck_field_list ) | 
	UNIQUE CLUSTERING KEY k1 ( tck_field_list ) |
	CLUSTERING UNIQUE KEY k1 ( tck_field_list ) | 
	tck_or_not k1 ( tck_field_list ) |
	tck_or_not k1 ( tck_field_list ) | 
	CONSTRAINT cs1 UNIQUE CLUSTERING KEY k1 ( tck_field_list ) | 
	CONSTRAINT cs1 CLUSTERING UNIQUE KEY k1 ( tck_field_list ) | 
	CONSTRAINT cs1 tck_or_not k1 ( tck_field_list ) | 
	CONSTRAINT cs1 tck_or_not k1 ( tck_field_list ) ;

tck_or_not:
	tck_or_not1 tck_or_not2 tck_or_not3 | tck_or_not1 tck_or_not2 tck_or_not3 |
	tck_or_not1 tck_or_not2 tck_or_not3 | tck_or_not1 tck_or_not2 tck_or_not3 |
	tck_or_not2 tck_or_not1 tck_or_not3 | tck_or_not2 tck_or_not1 tck_or_not3 |
	tck_or_not3 tck_or_not1 tck_or_not2 | tck_or_not3 tck_or_not2 tck_or_not1 ; 

tck_or_not1:
	| CLUSTERING ;

tck_or_not2:
	| UNIQUE ;

tck_or_not3:
	| key_or_index ;

key_or_index:
	KEY | INDEX ;

tck_field_list:
	c1 , c2 , c3, c4 |
	c1 , c2 , c3 | c2, c3, c4 |
	c1 , c2 | c2, c3 | c3, c4 |
	c1 | c1 | c1 | c1 |
	c2 | c2 | c2 | c2 | 
	c3 | c3 | c3 | c3 |
	c4 | c4 | c4 | c4 ;

tck_or_other:
	tb_toku_clust_key | tb_toku_clust_key | tb_toku_clust_key | tb_toku_clust_key | tb_toku_clust_key | tb_toku_clust_key | _table ;

if_not_exists:
	| IF NOT EXISTS ;

full_text_index:
	CREATE TABLE if_not_exists tb_fti ( c1 INT unsigned NOT NULL AUTO_INCREMENT, c2 text_type , fti_keydef ) ENGINE = INNODB |
	CREATE TABLE if_not_exists tb_fti ( c1 INT , c2 text_type , fti_keydef) ENGINE = INNODB |
	CREATE TABLE if_not_exists tb_fti ( c1 INT , c2 text_type , c3 text ,fti_keydef) ENGINE = INNODB |
	CREATE TABLE if_not_exists tb_fti ( FTS_DOC_ID BIGINT UNSIGNED NOT NULL AUTO_INCREMENT, c2 text_type , PRIMARY KEY (FTS_DOC_ID) , FULLTEXT KEY fk1 (c2) ) ENGINE = INNODB |
	ALTER TABLE tb_fti ADD fti_keydef |
	ALTER TABLE tb_fti ADD fti_keydef |
	ALTER TABLE tb_fti DROP PRIMARY KEY |
	ALTER TABLE tb_fti DROP INDEX fti_key |
	INSERT INTO tb_fti (c2) SELECT description FROM mysql.help_topic |
	INSERT INTO tb_fti (c2) SELECT description FROM mysql.help_topic |
	INSERT priority_insert ign INTO tb_fti (c3) SELECT description FROM mysql.help_topic |
	INSERT priority_insert ign INTO tb_fti (c2,c3) SELECT description,description FROM mysql.help_topic |
	INSERT priority_insert ign INTO tb_fti (c2) SELECT description FROM mysql.help_topic |
	SELECT * FROM tb_fti WHERE MATCH(c2) AGAINST(_english) |
	SELECT * FROM tb_fti WHERE MATCH(c2) AGAINST(_english IN BOOLEAN MODE) |
	SELECT * FROM tb_fti WHERE MATCH(c2) AGAINST(_english WITH QUERY EXPANSION) |
	DELETE FROM tb_fti where order_by limit |
	OPTIMIZE TABLE tb_fti ;

i_s_fti:
	SET GLOBAL innodb_ft_aux_table=innodb_ft_aux_table_list |
	SET GLOBAL innodb_ft_aux_table=innodb_ft_aux_table_list |
	SET GLOBAL innodb_optimize_fulltext_only = onoff |
	SELECT * FROM INFORMATION_SCHEMA.INNODB_FT_CONFIG |
	SELECT * FROM INFORMATION_SCHEMA.INNODB_FT_BEING_DELETED |
	SELECT * FROM INFORMATION_SCHEMA.INNODB_FT_DELETED |
	SELECT * FROM INFORMATION_SCHEMA.INNODB_FT_INDEX_TABLE |
	SELECT * FROM INFORMATION_SCHEMA.INNODB_FT_DEFAULT_STOPWORD |
	SELECT COUNT(1) FROM INFORMATION_SCHEMA.INNODB_FT_CONFIG |
	SELECT COUNT(1) FROM INFORMATION_SCHEMA.INNODB_FT_BEING_DELETED |
	SELECT COUNT(1) FROM INFORMATION_SCHEMA.INNODB_FT_DELETED |
	SELECT COUNT(1) FROM INFORMATION_SCHEMA.INNODB_FT_INDEX_TABLE |
	SELECT COUNT(1) FROM INFORMATION_SCHEMA.INNODB_FT_DEFAULT_STOPWORD ;	

fti_keydef:
	PRIMARY KEY (c1) , FULLTEXT KEY fti_key (c2) | PRIMARY KEY (c1) , FULLTEXT KEY fti_key (c2) | 
	PRIMARY KEY (c1) , FULLTEXT KEY fti_key (c2,c3) | FULLTEXT KEY fti_key (c2) | 
	FULLTEXT KEY fti_key (c2) | FULLTEXT KEY fti_key (c2,c3) | 
	FULLTEXT KEY fti_key (c3) | PRIMARY KEY (c1) ;

fti_key:
	k1 | k2 | k3 | k4 | k5 ;

tb_fti:
	fti_t1 | fti_t2 | fti_t3 | fti_t4 | fti_t5 ;

innodb_ft_aux_table_list:
	'test/fti_t1' | 'test/fti_t2' | 'test/fti_t3' | 'test/fti_t4' | 'test/fti_t5' ;

text_type: 
	TINYTEXT | TINYTEXT | TINYTEXT | TINYTEXT | TEXT | TEXT | TEXT | TEXT | MEDIUMTEXT ;

thread_pool:
	SET GLOBAL thread_pool_idle_timeout = zero_to_ttsh | 
	SET GLOBAL thread_pool_high_prio_tickets = thousand_to_tts |
	SET GLOBAL thread_pool_max_threads = hundred_to_thousand |
	SET GLOBAL thread_pool_oversubscribe = three_to_twenty | 
	SET GLOBAL thread_pool_size = one_to_ten |
	SET GLOBAL thread_pool_high_prio_tickets=0 |
	SET scope thread_pool_high_prio_mode = thread_pool_high_prio_mode_list |
	SHOW GLOBAL STATUS LIKE 'threadpool_idle_threads' |
	SHOW GLOBAL STATUS LIKE 'threadpool_threads' ;

thread_pool_high_prio_mode_list:
	transactions | statements | none ;

audit_plugin:
	SET GLOBAL audit_log_policy = audit_policy |
	SET GLOBAL audit_log_flush = onoff ;

audit_policy:
	ALL | LOGINS | QUERIES | NONE ;

binlog_event:
	SET GLOBAL BINLOG_FORMAT = binlog_format_list |
	insert | update | delete | outfile_infile | master_statement | flush_log |
	SET GLOBAL BINLOG_FORMAT = binlog_format_list ;

binlog_format_list:
	STATEMENT | ROW | MIXED ;

master_statement:
	SHOW BINLOG EVENTS | 
	SHOW MASTER STATUS ;

flush_log:
	FLUSH LOGS;

archive_logs:
	SHOW ENGINE INNODB STATUS |
	SET GLOBAL INNODB_LOG_ARCHIVE = onoff |
	SET GLOBAL INNODB_LOG_ARCH_EXPIRE_SEC = _digit |
	PURGE ARCHIVED LOGS BEFORE _datetime |
	PURGE ARCHIVED LOGS BEFORE NOW() ;

fake_changes:
	SET SESSION INNODB_FAKE_CHANGES = onoff |
	SET scope AUTOCOMMIT = onoff ;

show:
	SHOW GLOBAL STATUS LIKE 'innodb_master_thread_active_loops' |
	SHOW GLOBAL STATUS LIKE 'innodb_master_thread_idle_loops' |
	SHOW GLOBAL STATUS LIKE 'innodb_mutex_os_waits' |
	SHOW GLOBAL STATUS LIKE 'innodb_mutex_spin_rounds' |
	SHOW GLOBAL STATUS LIKE 'innodb_mutex_spin_waits' |
	SHOW GLOBAL STATUS LIKE 'innodb_s_lock_os_waits' |
	SHOW GLOBAL STATUS LIKE 'innodb_s_lock_spin_rounds' |
	SHOW GLOBAL STATUS LIKE 'innodb_s_lock_spin_waits' |
	SHOW GLOBAL STATUS LIKE 'innodb_x_lock_os_waits' |
	SHOW GLOBAL STATUS LIKE 'innodb_x_lock_spin_rounds' |
	SHOW GLOBAL STATUS LIKE 'innodb_x_lock_spin_waits' |
	SHOW GLOBAL STATUS LIKE 'innodb_ibuf_discarded_delete_marks' |
	SHOW GLOBAL STATUS LIKE 'innodb_ibuf_discarded_deletes' |
	SHOW GLOBAL STATUS LIKE 'innodb_ibuf_discarded_inserts' |
	SHOW GLOBAL STATUS LIKE 'innodb_ibuf_free_list' |
	SHOW GLOBAL STATUS LIKE 'innodb_ibuf_merged_delete_marks' |
	SHOW GLOBAL STATUS LIKE 'innodb_ibuf_merged_deletes' |
	SHOW GLOBAL STATUS LIKE 'innodb_ibuf_merged_inserts' |
	SHOW GLOBAL STATUS LIKE 'innodb_ibuf_merges' |
	SHOW GLOBAL STATUS LIKE 'innodb_ibuf_segment_size' |
	SHOW GLOBAL STATUS LIKE 'innodb_ibuf_size' |
	SHOW GLOBAL STATUS LIKE 'innodb_lsn_current' |
	SHOW GLOBAL STATUS LIKE 'innodb_lsn_flushed' |
	SHOW GLOBAL STATUS LIKE 'innodb_lsn_last_checkpoint' |
	SHOW GLOBAL STATUS LIKE 'innodb_checkpoint_age' |
	SHOW GLOBAL STATUS LIKE 'innodb_checkpoint_max_age' |
	SHOW GLOBAL STATUS LIKE 'innodb_mem_adaptive_hash' |
	SHOW GLOBAL STATUS LIKE 'innodb_mem_dictionary' |
	SHOW GLOBAL STATUS LIKE 'innodb_mem_total' |
	SHOW GLOBAL STATUS LIKE 'innodb_buffer_pool_pages_LRU_flushed' |
	SHOW GLOBAL STATUS LIKE 'innodb_buffer_pool_pages_made_not_young' |
	SHOW GLOBAL STATUS LIKE 'innodb_buffer_pool_pages_made_young' |
	SHOW GLOBAL STATUS LIKE 'innodb_buffer_pool_pages_old' |
	SHOW GLOBAL STATUS LIKE 'innodb_descriptors_memory' |
	SHOW GLOBAL STATUS LIKE 'innodb_read_views_memory' |
	SHOW GLOBAL STATUS LIKE 'innodb_history_list_length' |
	SHOW GLOBAL STATUS LIKE 'innodb_max_trx_id' |
	SHOW GLOBAL STATUS LIKE 'innodb_oldest_view_low_limit_trx_id' |
	SHOW GLOBAL STATUS LIKE 'innodb_purge_trx_id' |
	SHOW GLOBAL STATUS LIKE 'innodb_purge_undo_no' |
	SHOW GLOBAL STATUS LIKE 'innodb_current_row_locks' ;

query_cache:
	SET GLOBAL query_cache_strip_comments = onoff ;

# https://bugs.launchpad.net/percona-server/+bug/1367922 exclusions:
#	INFORMATION_SCHEMA.GLOBAL_TEMPORARY_TABLES |
#	INFORMATION_SCHEMA.TEMPORARY_TABLES |
i_s_area:
	INFORMATION_SCHEMA.PROCESSLIST | 
	INFORMATION_SCHEMA.XTRADB_RSEG ;

i_s:
	SELECT COUNT(1) FROM i_s_area | SELECT COUNT(1) FROM i_s_area |
	SELECT * FROM i_s_area LIMIT _digit | SELECT * FROM i_s_area LIMIT _digit |
	SELECT * FROM i_s_area ;

i_s_buffer_pool_stats:
	SELECT PAGE_TYPE,COUNT(1) FROM INNODB_BUFFER_PAGE_LRU GROUP BY PAGE_TYPE |
	SELECT SUM(DATA_SIZE) FROM INNODB_BUFFER_PAGE_LRU |
	SELECT SUM(DATA_SIZE) FROM INNODB_BUFFER_PAGE |
	SELECT POOL_ID,SUM(FREE_BUFFERS),SUM(NUMBER_PAGES_CREATED),SUM(NUMBER_PAGES_WRITTEN) FROM INNODB_BUFFER_POOL_STATS GROUP BY POOL_ID |
	SELECT PAGES_READ_RATE,PAGES_CREATE_RATE, PAGES_WRITTEN_RATE,HIT_RATE,READ_AHEAD_RATE,READ_AHEAD_EVICTED_RATE FROM INNODB_BUFFER_POOL_STATS |
	SELECT LRU_IO_TOTAL,LRU_IO_CURRENT,UNCOMPRESS_TOTAL,UNCOMPRESS_CURRENT FROM INNODB_BUFFER_POOL_STATS ;

# This could be merged into i_s and i_s_area
i_s_toku:
	SELECT dictionary_name,internal_file_name,checkpoint_count,blocknum,offset,size FROM INFORMATION_SCHEMA.TokuDB_fractal_tree_block_map LIMIT _digit |
	SELECT requesting_trx_id,blocking_trx_id,lock_waits_dname,lock_waits_key_left,lock_waits_key_right,lock_waits_start_time FROM INFORMATION_SCHEMA.TokuDB_lock_waits LIMIT _digit |
	SELECT dictionary_name,internal_file_name,table_schema,table_name,table_dictionary_name FROM INFORMATION_SCHEMA.TokuDB_file_map LIMIT _digit |
	SELECT locks_trx_id,locks_mysql_thread_id,locks_dname,locks_key_left,locks_key_right FROM INFORMATION_SCHEMA.TokuDB_locks LIMIT _digit |
	SELECT dictionary_name,internal_file_name,bt_num_blocks_allocated,bt_num_blocks_in_use,bt_size_allocated,bt_size_in_use FROM INFORMATION_SCHEMA.TokuDB_fractal_tree_info LIMIT _digit |
	SELECT trx_id,trx_mysql_thread_id FROM INFORMATION_SCHEMA.TokuDB_trx LIMIT _digit ;
	SELECT * FROM INFORMATION_SCHEMA.TokuDB_fractal_tree_block_map |
	SELECT * FROM INFORMATION_SCHEMA.TokuDB_lock_waits |
	SELECT * FROM INFORMATION_SCHEMA.TokuDB_file_map |
	SELECT * FROM INFORMATION_SCHEMA.TokuDB_locks |
	SELECT * FROM INFORMATION_SCHEMA.TokuDB_fractal_tree_info |
	SELECT * FROM INFORMATION_SCHEMA.TokuDB_trx |
	SELECT COUNT(1) FROM INFORMATION_SCHEMA.TokuDB_fractal_tree_block_map |
	SELECT COUNT(1) FROM INFORMATION_SCHEMA.TokuDB_lock_waits |
	SELECT COUNT(1) FROM INFORMATION_SCHEMA.TokuDB_file_map |
	SELECT COUNT(1) FROM INFORMATION_SCHEMA.TokuDB_locks |
	SELECT COUNT(1) FROM INFORMATION_SCHEMA.TokuDB_fractal_tree_info |
	SELECT COUNT(1) FROM INFORMATION_SCHEMA.TokuDB_trx ;

engine:
	INNODB|INNODB|INNODB|INNODB|TOKUDB;

bitmap:
	SHOW ENGINE INNODB MUTEX |
	SELECT start_lsn, end_lsn, space_id, page_id FROM INFORMATION_SCHEMA.INNODB_CHANGED_PAGES LIMIT _digit |
	SELECT COUNT(*) FROM INFORMATION_SCHEMA.INNODB_CHANGED_PAGES |
	FLUSH CHANGED_PAGE_BITMAPS | FLUSH CHANGED_PAGE_BITMAPS |
	RESET CHANGED_PAGE_BITMAPS | RESET CHANGED_PAGE_BITMAPS |
	PURGE CHANGED_PAGE_BITMAPS BEFORE _digit | PURGE CHANGED_PAGE_BITMAPS BEFORE _digit |
	SET GLOBAL INNODB_MAX_CHANGED_PAGES = _digit | SET GLOBAL INNODB_MAX_CHANGED_PAGES = 0 |
	bitmap_ods ;

bitmap_ods:
	PURGE CHANGED_PAGE_BITMAPS BEFORE 0 | PURGE CHANGED_PAGE_BITMAPS BEFORE 1 |
	PURGE CHANGED_PAGE_BITMAPS BEFORE NULL | PURGE CHANGED_PAGE_BITMAPS BEFORE (SELECT (1)) |
	PURGE CHANGED_PAGE_BITMAPS BEFORE -1 | PURGE CHANGED_PAGE_BITMAPS BEFORE 18446744073709551615 |
	SET GLOBAL INNODB_MAX_CHANGED_PAGES = 1 | SET GLOBAL INNODB_MAX_CHANGED_PAGES = NULL |
	SET GLOBAL INNODB_MAX_CHANGED_PAGES = -1 | SET GLOBAL INNODB_MAX_CHANGED_PAGES = 18446744073709551615 |
	SELECT COUNT(*) FROM INFORMATION_SCHEMA.INNODB_CHANGED_PAGES GROUP BY END_LSN ORDER BY END_LSN LIMIT 1 |
	SELECT * FROM INNODB_CHANGED_PAGES WHERE START_LSN > _digit AND END_LSN <= _digit AND _digit > END_LSN AND PAGE_ID = _digit LIMIT 10 |
	SELECT COUNT(*) FROM INFORMATION_SCHEMA.INNODB_CHANGED_PAGES WHERE START_LSN >= END_LSN ;

kill_idle:
	SET GLOBAL innodb_kill_idle_transaction = kit_list ;

kit_list:
	0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1 | 20 | 60 ;

scope:
	GLOBAL | SESSION ;

ext_slow_query_log:
	SET scope LOG_SLOW_FILTER = log_slow_filter_list |
	SET scope LOG_SLOW_RATE_LIMIT = 0 | 
	SET scope LOG_SLOW_RATE_LIMIT = _digit |
	SET scope LOG_SLOW_VERBOSITY = log_slow_verbosity_list | 
	SET scope LONG_QUERY_TIME = _digit |
	SET GLOBAL LOG_SLOW_SLAVE_STATEMENTS = onoff | 
	SET GLOBAL LOG_SLOW_RATE_TYPE = log_slow_rate_type_list |
	SET GLOBAL LOG_SLOW_SP_STATEMENTS = onoff | 
	SET GLOBAL SLOW_QUERY_LOG_TIMESTAMP_ALWAYS = onoff | 
	SET GLOBAL SLOW_QUERY_LOG_TIMESTAMP_PRECISION = slow_query_log_timestamp_precision_list | 
	SET GLOBAL SLOW_QUERY_LOG_USE_GLOBAL_CONTROL = slow_query_log_use_global_control_list ;

log_slow_filter_list:
	QC_MISS | FULL_SCAN | FULL_JOIN | TMP_TABLE | TMP_TABLE_ON_DISK | FILESORT | FILESORT_ON_DISK | "" ;

log_slow_rate_type_list:
	SESSION | QUERY ;

log_slow_verbosity_list:
	MICROTIME | QUERY_PLAN | INNODB | FULL | PROFILING | PROFILING_USE_GETRUSAGE | "" ;

slow_query_log_timestamp_precision_list:
	SECOND | MICROSECOND ;

slow_query_log_use_global_control_list:
	LOG_SLOW_FILTER | LOG_SLOW_RATE_LIMIT | LOG_SLOW_VERBOSITY | LONG_QUERY_TIME | MIN_EXAMINED_ROW_LIMIT | ALL | "" ;

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
	USER | TOTAL_CONNECTIONS | CONCURRENT_CONNECTIONS | CONNECTED_TIME | BUSY_TIME | CPU_TIME | 
	BYTES_RECEIVED | BYTES_SENT | BINLOG_BYTES_WRITTEN | ROWS_FETCHED | ROWS_UPDATED | TABLE_ROWS_READ | 
	SELECT_COMMANDS | UPDATE_COMMANDS | OTHER_COMMANDS | COMMIT_TRANSACTIONS | ROLLBACK_TRANSACTIONS | 
	DENIED_CONNECTIONS | LOST_CONNECTIONS | ACCESS_DENIED | EMPTY_QUERIES | TOTAL_SSL_CONNECTIONS |
	user_stats_1 , user_stats_1 | user_stats_1, user_stats_1 | * ;

user_stats_2:
	TABLE_SCHEMA | TABLE_NAME | ROWS_READ | ROWS_CHANGED | ROWS_CHANGED_X_INDEXES |
	user_stats_2 , user_stats_2 | * ;

user_stats_3:
	TABLE_SCHEMA | TABLE_NAME | INDEX_NAME | ROWS_READ |
	user_stats_3 , user_stats_3 | * ;

user_stats_4:
	CLIENT | TOTAL_CONNECTIONS | CONCURRENT_CONNECTIONS | CONNECTED_TIME | BUSY_TIME | CPU_TIME | 
	BYTES_RECEIVED | BYTES_SENT | BINLOG_BYTES_WRITTEN | ROWS_FETCHED | ROWS_UPDATED | TABLE_ROWS_READ | 
	SELECT_COMMANDS | UPDATE_COMMANDS | OTHER_COMMANDS | COMMIT_TRANSACTIONS | ROLLBACK_TRANSACTIONS | 
	DENIED_CONNECTIONS | LOST_CONNECTIONS | ACCESS_DENIED | EMPTY_QUERIES | TOTAL_CONNECTIONS_SSL |
	user_stats_4 , user_stats_4 | user_stats_4, user_stats_4 | * ;

flush_user_stats:
	FLUSH CLIENT_STATISTICS | FLUSH INDEX_STATISTICS | FLUSH TABLE_STATISTICS | FLUSH THREAD_STATISTICS | FLUSH USER_STATISTICS ;

show_user_stats:
	SHOW CLIENT_STATISTICS  | SHOW INDEX_STATISTICS  | SHOW TABLE_STATISTICS  | SHOW THREAD_STATISTICS  | SHOW USER_STATISTICS ;

action:
	ASSERT | WARN | SALVAGE ;

onoff:
	1 | 0 ;	

moreoff:
	0 | 0 | 0 | 0 | 1 ;

moreon:
	1 | 1 | 0 ;

truefalse:
	TRUE | TRUE | FALSE ;

# This is re-defined for debug only (in 5.6_debug.yy.redef) as certain statements fail in debug only
# See https://bugs.launchpad.net/percona-server/+bug/1368530
set:
	SET GLOBAL innodb_show_verbose_locks = onoff | 
	SET GLOBAL innodb_show_locks_held = zero_to_thousand |
	SET GLOBAL INNODB_USE_GLOBAL_FLUSH_LOG_AT_TRX_COMMIT = onoff  |
	SET GLOBAL INNODB_CORRUPT_TABLE_ACTION = action |
	SET scope INNODB_STRICT_MODE = onoff |
	SET scope OLD_ALTER_TABLE = onoff |
	SET scope EXPAND_FAST_INDEX_CREATION = ON |
	SET scope EXPAND_FAST_INDEX_CREATION = OFF |
	SET @@GLOBAL.innodb_log_checkpoint_now = truefalse |
	SET @@GLOBAL.innodb_track_redo_log_now = truefalse |
	SET @@GLOBAL.innodb_track_changed_pages = truefalse |
	SET GLOBAL innodb_empty_free_list_algorithm = innodb_empty_free_list_algo |
	SET GLOBAL innodb_log_checksum_algorithm = innodb_log_checksum_algorithm_list ;

innodb_log_checksum_algorithm_list:
	none | innodb | crc32 | strict_none | strict_innodb | strict_crc32 ;

innodb_empty_free_list_algo:
	legacy | backoff ;

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
	CREATE TABLE if_not_exists tb_comp ( c1 type null_or_not , c2 type default_or_not , c3 type , c4 type null_or_not default_or_not , tb_keydef ) ENGINE = engine ROW_FORMAT = row_format KEY_BLOCK_SIZE = kb_size |
	CREATE TABLE if_not_exists tb_comp ( c1 type null_or_not , c2 type default_or_not , c3 type , c4 type null_or_not default_or_not , tb_keydef ) ENGINE = engine |
	CREATE TABLE if_not_exists tb_comp ( c1 type null_or_not , c2 type default_or_not , c3 type null_or_not default_or_not , tb_keydef ) ENGINE = engine |
	CREATE TABLE if_not_exists tb_comp ( c1 type null_or_not , c2 type default_or_not , c3 type null_or_not default_or_not ) ENGINE = engine |
	DROP TABLE tb_comp | DROP TABLE tb_comp | DROP TABLE tb_comp |
	INSERT INTO tb_comp VALUES ( value , value , value , value ) |
	INSERT INTO tb_comp VALUES ( value , value , value , value ) |
	INSERT INTO tb_comp VALUES ( value , value , value ) |
	ALTER TABLE tb_comp_plus ROW_FORMAT = row_format |
	ALTER TABLE tb_comp_plus ROW_FORMAT = row_format KEY_BLOCK_SIZE = kb_size |
	ALTER TABLE tb_comp_plus KEY_BLOCK_SIZE = kb_size |
	ALTER TABLE tb_comp_plus DROP PRIMARY key_or_index |
	ALTER TABLE tb_comp_plus ADD tb_keydef ;

tb_comp:
	t1 | t2 | t3 | t4 | t5 | t6 | t7 | t8 | t9 | _table ;

tb_comp_plus:
	_table | _table | tb_comp ;

row_format:
	COMPRESSED | COMPRESSED | COMPRESSED |
	DEFAULT | DEFAULT | DYNAMIC | FIXED | COMPACT |
	TOKUDB_DEFAULT | TOKUDB_FAST | TOKUDB_SMALL |
	TOKUDB_DEFAULT | TOKUDB_FAST | TOKUDB_SMALL ;

tb_keydef:
	PRIMARY key_or_index (c1) , key_or_index (c2) hash_or_not |
	PRIMARY key_or_index (c3,c4) , key_or_index (c2) hash_or_not |
	PRIMARY key_or_index (c2) hash_or_not |
	PRIMARY key_or_index (c4,c3) hash_or_not |
	PRIMARY key_or_index (c4,c3) hash_or_not KEY_BLOCK_SIZE = kb_size |
	UNIQUE (c4,c3) hash_or_not |
	key_or_index (c1(1)) ;

hash_or_not:
	| | | | | USING HASH | USING BTREE ;

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

# Errors: fix later 
#algo:
#	| | ALGORITHM = DEFAULT | ALGORITHM = INPLACE | ALGORITHM = COPY ;
#
#lock_type:
#	| | LOCK = DEFAULT | LOCK = NONE | LOCK = SHARED | LOCK = EXCLUSIVE ;

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
	UNLOCK TABLES ; UNLOCK BINLOG | 
	UNLOCK TABLES ; UNLOCK BINLOG | 
	UNLOCK TABLES ; UNLOCK BINLOG | 
	UNLOCK TABLES ; UNLOCK BINLOG | 
	UNLOCK TABLES ; UNLOCK BINLOG | 
	UNLOCK TABLES | UNLOCK BINLOG | 
	UNLOCK TABLES | UNLOCK BINLOG | 
	lock_function ;

lock_function:
	LOCK TABLE _table READ | LOCK TABLE _table WRITE |
	LOCK TABLE _letter READ | LOCK TABLE _letter WRITE |
	LOCK TABLE _table AS _letter READ | LOCK TABLE _table as _letter WRITE |
	LOCK TABLE _table READ LOCAL | LOCK TABLE _table LOW_PRIORITY WRITE |
	LOCK TABLE _table AS _letter READ LOCAL | LOCK TABLE _table as _letter LOW_PRIORITY WRITE |
	FLUSH TABLES _table FOR EXPORT | FLUSH TABLES _letter FOR EXPORT |
	FLUSH TABLES WITH READ LOCK |
	FLUSH TABLES _table WITH READ LOCK | FLUSH TABLES _letter WITH READ LOCK |
	LOCK TABLES FOR BACKUP | LOCK BINLOG FOR BACKUP ;

proc_param:
	IN _letter type | OUT _letter type ;

views:
	DROP TABLE IF EXISTS _letter[invariant] ; DROP VIEW IF EXISTS _letter[invariant] ; CREATE VIEW _letter[invariant] AS SELECT * FROM _table ; INSERT INTO _letter[invariant] ( _field ) VALUES ( value ) ;
	
outfile_infile:
	SELECT * FROM _table[invariant] INTO OUTFILE _tmpnam ; TRUNCATE _table[invariant] ; LOAD DATA INFILE _tmpnam INTO TABLE _table[invariant] ;
	SELECT * FROM _table[invariant] INTO OUTFILE _tmpnam ; TRUNCATE _table[invariant] ; LOAD DATA LOCAL INFILE _tmpnam INTO TABLE _table[invariant] ;

null_or_not:
	| | NULL | NOT NULL ;

default_or_not:
	| | DEFAULT 0 | DEFAULT NULL | DEFAULT 1 | DEFAULT 'a' ;

type:
	INT | INT AUTO_INCREMENT | DECIMAL | FLOAT | DOUBLE | DECIMAL( _digit , _digit ) | BIT | CHAR( _digit ) | VARCHAR( _digit ) | 
	BLOB | BLOB | DATE | DATETIME | TIMESTAMP | TIME | YEAR | BINARY | TEXT | ENUM('a','b','c') | SET('a','b','c') ;

value:
	_digit | 0 | 1 | -1 | _data | _bigint_unsigned | _bigint | _mediumint | _english | _letter | 
	_char | _varchar |_date | _year | _time | _datetime | _timestamp | NULL | NULL | NULL ;
