thread1_init:
	ANALYZE TABLE { join ',', @{$executors->[0]->baseTables()} };

thread4_init:
	PREPARE show_expl_stmt FROM "SHOW EXPLAIN FOR ?"; 

thread4:
	show_explain |
	explain_iud |
	engine_independent_statistics |
	flush_export |
	create_or_replace
;

thread7:
	thread4;

thread3:
	set_names_or_charset | 
	query | query | query | query | query | query ;

#############################
# CREATE OR REPLACE

create_or_replace:
	CREATE OR REPLACE temporary_for_create_or_replace TABLE `create_or_replace_tmp` AS SELECT * FROM _table |
	lock_for_create_or_replace ; CREATE OR REPLACE temporary_for_create_or_replace TABLE `create_or_replace_tmp` LIKE _basetable[invariant] ; INSERT INTO `create_or_replace_tmp` SELECT * FROM _basetable[invariant] ; UNLOCK TABLES;

lock_for_create_or_replace:
	| LOCK TABLE `create_or_replace_tmp` WRITE, _basetable[invariant] READ ; 

temporary_for_create_or_replace:
	| TEMPORARY ;

#############################
# FLUSH TABLES .. FOR EXPORT

# Use a table only once in FLUSH command

flush_export:
	{ @tables = shuffle(@{$executors->[0]->baseTables()}); '' } FLUSH TABLE flush_table_list FOR EXPORT ; UNLOCK TABLES ;

flush_table_list:
	flush_table_name | flush_table_list, flush_table_name ;

flush_table_name:
	{ pop @tables } ;

#############################
# SHOW EXPLAIN

show_explain:
	SELECT ID INTO @thread_id FROM INFORMATION_SCHEMA.PROCESSLIST ORDER BY RAND() LIMIT 1 ; EXECUTE show_expl_stmt USING @thread_id ;

#############################
# EXPLAIN UPDATE/DELETE/INSERT

explain_iud:
	EXPLAIN explain_iud_extended query ;

explain_iud_extended:
	| EXTENDED ;

#############################
# Engine-independent statistics

engine_independent_statistics:
	eistat_analyze_stats | eistat_analyze_stats | eistat_analyze_stats | eistat_analyze_stats | 
	eistat_analyze_stats | eistat_analyze_stats | eistat_analyze_stats | eistat_analyze_stats |
	eistat_analyze_stats | eistat_analyze_stats | eistat_analyze_stats | eistat_analyze_stats |
	eistat_analyze_stats | eistat_analyze_stats | eistat_analyze_stats | eistat_analyze_stats |
	eistat_set_use_stat_tables | eistat_set_use_stat_tables | 
	eistat_select_stat ;

eistat_analyze_stats:
	ANALYZE TABLE _table eistat_persistent_for |
	ANALYZE TABLE _basetable PERSISTENT FOR COLUMNS eistat_persistent_for_columns INDEXES  eistat_persistent_for_real_indexes
	;

eistat_persistent_for:
	| 
	PERSISTENT FOR ALL |
	PERSISTENT FOR COLUMNS eistat_persistent_for_columns INDEXES eistat_persistent_for_columns ;

eistat_persistent_for_columns:
	ALL | ( eistat_columns ) ;

eistat_persistent_for_real_indexes:
	ALL | ( eistat_indexes ) ;
	
eistat_columns:
	 | | _field | _field | _field, _field, _field, _field ;

eistat_indexes:
	 | | 
	PRIMARY |
	eistat_indexed | eistat_indexed | 
	eistat_indexed, eistat_indexed, eistat_indexed, eistat_indexed |
# Hoping to create an existing multi-part index name sometimes
	{ '`'.$prng->arrayElement($fields_indexed).'_'.$prng->arrayElement($fields_indexed).'`' }
;

eistat_indexed:
	_field_key | _field_key | _field_key | _field_key | _field ;

eistat_set_use_stat_tables:
	SET eistat_scope use_stat_tables = eistat_use_stat_tables ;

eistat_use_stat_tables:
	PREFERABLY | COMPLEMENTARY | NEVER ;

eistat_scope:
	| SESSION | GLOBAL ;

eistat_stat_table:
	`mysql` . `table_stats` | `mysql` . `column_stats` | `mysql` . `index_stats` ;

eistat_select_stat:
	SELECT * FROM eistat_stat_table WHERE `table_name` = '_table' ;

set_names_or_charset:
	SELECT CHARACTER_SET_NAME INTO @cset FROM INFORMATION_SCHEMA.CHARACTER_SETS ORDER BY RAND() LIMIT 1; names_or_charset ; PREPARE stmt_names_or_charset FROM @stmt_names_or_charset ; EXECUTE stmt_names_or_charset ; DEALLOCATE PREPARE stmt_names_or_charset |
	SET NAMES DEFAULT |
	SET CHARACTER SET DEFAULT ;

names_or_charset:
	SET @stmt_names_or_charset = CONCAT( 'SET NAMES ', @cset ); add_collation |
	SET @stmt_names_or_charset = CONCAT( 'SET CHARACTER SET ', @cset );

add_collation:
	valid_collation | valid_collation | valid_collation | valid_collation | valid_collation | valid_collation | 
	invalid_collation ;

valid_collation:
	SELECT CONCAT(@stmt_names_or_charset, ' COLLATE `', COLLATION_NAME, '`') INTO @stmt_names_or_charset FROM INFORMATION_SCHEMA.COLLATIONS WHERE CHARACTER_SET_NAME = @cset ORDER BY RAND() LIMIT 1;

invalid_collation:
        SELECT CONCAT(@stmt_names_or_charset, ' COLLATE `', COLLATION_NAME, '`') INTO @stmt_names_or_charset FROM INFORMATION_SCHEMA.COLLATIONS WHERE CHARACTER_SET_NAME != @cset ORDER BY RAND() LIMIT 1;

