#
# The goal of this grammar is to stress test the operation of the HEAP storage engine by:
# 
# * Creating numerous tables, populating them rapidly and then dropping them
#
# * Using various DDL statements that cause HEAP tables to be created or manipulated
#
# * Have concurrent operation by using mostly TEMPORARY or connection-specific tables
#

query_init:
	{ $table_name = 'local_'.$generator->threadId().'_1' ; return undef; } create_definition_init ;	{ $table_name = 'local_'.$generator->threadId().'_2' ; return undef; } create_definition_init ; { $table_name = 'local_'.$generator->threadId().'_3' ; return undef; } create_definition_init ;

create_definition_init:
	create_definition SELECT short_value  AS f1 , short_value AS f2 , short_value AS f3 , short_value AS f4 , short_value AS f5 FROM DUAL ;

query:
	create_drop |
	select | select | select | select |
	insert | insert | insert | insert |
	insert | insert | insert | insert |
	update | update | update | update |
	delete | delete | delete | delete |
	alter |
	truncate ;

create_drop:
	set_table_name DROP TABLE IF EXISTS { $table_name } ; create_definition ; create_definition ; create_definition |
	set_table_name DROP TABLE IF EXISTS { $table_name } ; create_definition select_all ;

alter:
	ALTER TABLE table_name ENGINE = HEAP |
	ALTER TABLE table_name enable_disable KEYS ;

enable_disable:
	ENABLE | DISABLE ;

truncate:
	TRUNCATE TABLE table_name ;

select_all:
	SELECT * FROM table_name ;

create_definition:
	CREATE temporary TABLE IF NOT EXISTS { $table_name } (
		f1 column_def_index ,
		f2 column_def_index ,
		f3 column_def ,
		f4 column_def ,
		f5 column_def ,
		index_definition_list
	) ENGINE=HEAP /*executor1 ROW_FORMAT = dynamic_fixed KEY_BLOCK_SIZE = key_block_size */ ;

temporary:
	| | | | | | TEMPORARY ;

dynamic_fixed:
	DYNAMIC | DYNAMIC | DYNAMIC | DYNAMIC | DYNAMIC |
	DYNAMIC | DYNAMIC | DYNAMIC | DYNAMIC | FIXED ;

insert:
	insert_multi | insert_multi | insert_select ;

insert_multi:
	INSERT IGNORE INTO table_name VALUES row_list ;

insert_select:
	INSERT IGNORE INTO table_name select_all;

row_list:
	row , row , row , row |
	row_list , row ;

row:
	( value , value , value , value , value ) ;

index_definition_list:
	index_definition |
	index_definition , index_definition ;

index_definition:
	index_type ( index_column_list ) USING btree_hash ;

btree_hash:
	BTREE | HASH ;

index_type:
	KEY | KEY | KEY | KEY | PRIMARY KEY ;

index_column_list:
	f1 | f2 | f1 , f2 | f2 , f1 |
	f1 ( index_column_size ) | f2 ( index_column_size ) |
	f1 ( index_column_size ) , f2 ( index_column_size ) |
	f2 ( index_column_size ) , f1 ( index_column_size ) ;	

index_column_size:
	1 | 2 | 32 ;

key_block_size:
	512 | 1024 | 2048 | 3072 ;

column_def:
	VARCHAR ( size_varchar ) character_set not_null default |
	VARCHAR ( size_varchar ) collation not_null default |
	VARBINARY ( size_varchar ) |
	blob not_null |
	blob not_null |
	blob not_null ;

character_set:
	| CHARACTER SET utf8 ;

collation:
	| COLLATE utf8_bin ;

column_def_index:
	VARCHAR ( size_index ) character_set not_null default |
	VARCHAR ( size_index ) collation not_null default ;

size_varchar:
	32 | 128 | 512 | 1024  ;

size_index:
	32 | 128 ;

blob:
	BLOB | BLOB ( blob_size ) | MEDIUMBLOB | TINYBLOB | LONGBLOB |
	TEXT character_set |
	TEXT collation |
	TEXT ( blob_size ) character_set  |
	TEXT ( blob_size ) collation |
	MEDIUMTEXT | TINYTEXT | LONGTEXT ;

blob_size:
	1024 | 65525 ;	

not_null:
	| NOT NULL ;

default:
	| DEFAULT _varchar(32) ;

unique:
	| UNIQUE ;

table_name:
	connection_specific_table |
	connection_specific_table |
	connection_specific_table |
	connection_specific_table |
	connection_specific_table |
	global_table ;

connection_specific_table:
	{ 'local_'.$generator->threadId().'_'.$prng->int(1,5) } ;

global_table:
	global_1 | global_2 | global_3 | global_4 | global_5 ;

set_table_name:
	{ $table_name = $prng->int(1,5) <= 4 ? 'local_'.$generator->threadId().'_'.$prng->int(1,5) : 'global_'.$prng->int(1,5) ; return undef ; } ;

value_list:
	value , value |
	value , value_list ;

value:
	short_value | long_value ;

short_value:
	_digit | _varchar(1) | NULL | _english ;

long_value:
	REPEAT( _varchar(128) , _digit ) | NULL | _data ;

select:
	SELECT field_name FROM table_name WHERE where order_by ;

order_by:
	ORDER BY field_name desc_asc ;

desc_asc:
	DESC | ASC ;

update:
	UPDATE table_name SET field_name = value WHERE where ;

delete:
	DELETE FROM table_name WHERE where ;

field_name:
	f1 | f2 | f3 | f4 | f5 ;

where:
	(field_name cmp_op value ) and_or where |
	field_name cmp_op value |
	field_name not IN ( value_list ) |
	field_name BETWEEN value AND value ;

and_or:
	AND | OR ;

not:
	| NOT ;

cmp_op:
	< | > | = | <= | >= | <> | <=> | != ;
