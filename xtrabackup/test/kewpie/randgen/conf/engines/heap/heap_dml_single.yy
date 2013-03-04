#
# The goal of this grammar is to stress test the operation of the HEAP storage engine by:
#
# * Creating a small set of tables and executing various operations over those tables
#
# * Employ TEMPORARY tables in as many DML contexts as possible
# 
# This grammar goes together with the respective mysqld --init file that creates the tables
#

query:
	insert | insert | insert |
	select | delete | update ;

select:
	SELECT select_list FROM table_name any_where |
	SELECT select_list FROM table_name restrictive_where order_by |
	SELECT select_list FROM table_name restrictive_where full_order_by LIMIT _digit |
	SELECT field_name FROM table_name any_where ORDER BY field_name DESC ;

select_list:
	field_name |
	field_name , select_list ;

delete:
	DELETE FROM table_name restrictive_where |
	DELETE FROM table_name restrictive_where |
	DELETE FROM table_name restrictive_where |
	DELETE FROM table_name restrictive_where |
	DELETE FROM table_name any_where full_order_by LIMIT _digit |
	TRUNCATE TABLE table_name ;

update:
	UPDATE table_name SET update_list restrictive_where |
	UPDATE table_name SET update_list restrictive_where |
	UPDATE table_name SET update_list restrictive_where |
	UPDATE table_name SET update_list restrictive_where |
	UPDATE table_name SET update_list any_where full_order_by LIMIT _digit ;

any_where:
	permissive_where | restrictive_where;

restrictive_where:
	WHERE field_name LIKE(CONCAT( _varchar(2), '%')) |
	WHERE field_name = _varchar(2) |
	WHERE field_name LIKE(CONCAT( _varchar(1), '%')) AND field_name LIKE(CONCAT( _varchar(1), '%')) |
	WHERE field_name BETWEEN _varchar(2) AND _varchar(2) ;

permissive_where:
	WHERE field_name comp_op value |
	WHERE field_name comp_op value OR field_name comp_op value ;

comp_op:
	> | < | >= | <= | <> | != | <=> ;

update_list:
	field_name = value |
	field_name = value , update_list ;

insert:
	insert_single | insert_select |
	insert_multi | insert_multi | insert_multi ;

insert_single:
	INSERT IGNORE INTO table_name VALUES ( value , value , value , value ) ;

insert_multi:
	INSERT IGNORE INTO table_name VALUES value_list ;

insert_select:
	INSERT IGNORE INTO table_name SELECT * FROM table_name restrictive_where full_order_by LIMIT _tinyint_unsigned ;

order_by:
	| ORDER BY field_name ;

full_order_by:
	ORDER BY f1 , f2 , f3 , f4 ;

value_list:
	( value , value, value , value ) |
	( value , value, value , value ) , value_list |
	( value , value, value , value ) , value_list ;

value:
	small_value | large_value ;

small_value:
	_digit | _varchar(1) | _varchar(2) | _varchar(32) | NULL ;

large_value:
	_varchar(32) | _varchar(1024) | _data | NULL ;


field_name:
	f1 | f2 | f3 | f4 ;


table_name:
	heap_complex_indexes |
	heap_complex_indexes_hash |
	heap_large_block |
	heap_noindexes_large |
	heap_noindexes_small |
	heap_oversize_pk |
	heap_small_block |
	heap_standard |
	heap_blobs |
	heap_char |
	heap_other_types |
	heap_fixed ;
