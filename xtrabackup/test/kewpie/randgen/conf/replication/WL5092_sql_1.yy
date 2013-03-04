query_init:
	{ $count = 0 ; return undef } CREATE SCHEMA test1; CREATE SCHEMA test2;

query:
	pick_parameters drop_table ; create_table ;

drop_table:
	DROP TABLE IF EXISTS $database . $tablename ;
create_table:
   CREATE TABLE $database . $tablename ENGINE = $engine AS SELECT _field_list FROM _table WHERE 1 = 2; ALTER TABLE $database . $tablename key_structure ;
pick_parameters:
	{ $engine = $prng->arrayElement(['myisam','innodb']) ; $database = $prng->arrayElement(['test1','test2']) ; $count++ ; if ($count > 20) { $count = 1 } ; $tablename = 't1_'.$count.'_'.$engine ; return undef }; 

# Manual about indexes:
# - Prefixes can be specified for CHAR, VARCHAR, BINARY, and VARBINARY columns. 
# - BLOB and TEXT columns also can be indexed, but a prefix length must be given. 
# - A UNIQUE index creates a constraint such that all values in the index must be distinct.
#   An error occurs if you try to add a new row with a key value that matches an existing row.
#   For all engines, a UNIQUE index allows multiple NULL values for columns that (are allowed to)
#   can contain NULL.
# - In MySQL 5.5:
#   - You can add an index on a column that can have NULL values only if you are using the MyISAM,
#     InnoDB, or MEMORY storage engine.
#   - You can add an index on a BLOB or TEXT column only if you are using the MyISAM, or InnoDB
#     storage engine.
# - If you specify a prefix value for a column in a UNIQUE index, the column values must be unique
#   within the prefix. 
#

key_structure:
   # a) PRIMARY KEY exists, no UNIQUE INDEX
	ADD PRIMARY KEY ( no_prefix_idx_not_null_columns )                                    | # a1) PK = NB
	ADD PRIMARY KEY ( prefix_idx_not_null_columns (25))                                   | # a2) PK = partial B
   ADD PRIMARY KEY ( no_prefix_idx_not_null_columns , no_prefix_idx_not_null_columns )   | # a3) PK = NB, NB
	ADD PRIMARY KEY ( prefix_idx_not_null_columns (25), prefix_idx_not_null_columns (25)) | # a4) PK = partial B, partial B
   ADD PRIMARY KEY ( no_prefix_idx_not_null_columns , prefix_idx_not_null_columns (25))  | # a5) PK = NB, partial B      
	# b) UNIQUE INDEX exists, no PRIMARY KEY
	#    Attention: In such a situation InnoDB clusters the table rows according to the first UNIQUE INDEX with NOT NULL found.
	#               This means in case of InnoDB and UNIQUE INDEX with NOT NULL we check in fact what we already had in a).
   #               So it makes sense to use here only columns which could be NULL.
	#               This does not limit the coverage for storage engines like MyISAM (no clustering of rows at all) because
	#               they generate the same physical key structure for PRIMARY KEY and UNIQUE INDEX NOT NULL.
	#            clustered PRIMARY KEY.
	ADD UNIQUE INDEX idx1 ( no_prefix_idx_null_columns )                                  | # b1) UI = NB
	ADD UNIQUE INDEX idx1 ( prefix_idx_null_columns (25))                                 | # b2) UI = partial B
	ADD UNIQUE INDEX idx1 ( no_prefix_idx_null_columns , no_prefix_idx_null_columns )     | # b3) UI = NB, NB
   ADD UNIQUE INDEX idx1 ( prefix_idx_null_columns (25), prefix_idx_null_columns (25))   | # b4) UI = partial B, partial B
	ADD UNIQUE INDEX idx1 ( no_prefix_idx_null_columns , prefix_idx_null_columns (25))    | # b5) UI = NB, partial B
	# c) no PRIMARY KEY or UNIQUE INDEX exists
	#    Some content within the ALTER TABLE is needed. Therefore I use COMMENT here.
   COMMENT 'no PRIMARY KEY or INDEX'                                                     | # c) no UI, no PK
   # d) Special interest, but it is rather unlikely that these scenarios have an impact on binary logging.
	# d1) Two UNIQUE INDEXes
   #     1. Every of them can be used to distinct one row from all others.
	#     2. An optimizer might decide that merging both indexes is the best way when searching the rows.
   ADD UNIQUE INDEX idx1 ( no_prefix_idx_null_columns ), ADD UNIQUE INDEX idx2 ( no_prefix_idx_null_columns ) |
	# d2) two non UNIQUE INDEXes
   #     1. Under certain conditions one of them or both can be used to distinct one row from all others.
   #     2. An optimizer might decide that using one of them or both is the best way when searching the rows.
   ADD INDEX idx1 ( no_prefix_idx_null_columns ), ADD UNIQUE INDEX idx2 ( no_prefix_idx_null_columns )        ;
   
prefix_idx_columns:
	prefix_idx_not_null_columns |
	prefix_idx_null_columns     ;

no_prefix_idx_columns:
	no_prefix_idx_not_null_columns |
	no_prefix_idx_null_columns     ;

no_prefix_idx_not_null_columns:
	pk                                 |
	col_bit_4_not_null                 |
	col_bit_64_not_null                |
	col_double_not_null                |
	col_tinyint_not_null               |
	col_bigint_not_null                |
	col_decimal_35_not_null            |
	col_char_25_binary_not_null        |
	col_char_25_not_null               |
	col_varchar_25_not_null            |
	col_varchar_25_binary_not_null     ;
	
prefix_idx_not_null_columns:
	col_tinytext_binary_not_null       |
	col_tinytext_not_null              ;

no_prefix_idx_null_columns:
	col_bit_4_default_null             |
	col_bit_64_default_null            |
	col_double_default_null            |
	col_tinyint_default_null           |
	col_bigint_default_null            |
	col_decimal_35_default_null        |
	col_char_25_binary_default_null    |
	col_char_25_default_null           |
	col_varchar_25_default_null        |
	col_varchar_25_binary_default_null ;

prefix_idx_null_columns:
	col_tinytext_binary_default_null   |
	col_tinytext_default_null          ;

database:
	test  |
	test1 |
	      ;

