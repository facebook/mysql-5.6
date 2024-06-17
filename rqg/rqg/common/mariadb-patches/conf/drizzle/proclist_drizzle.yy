query:
  { $tables = 0 ; $fields = 0 ; "" } query_type ;

query_type:
  normal_query | normal_query | normal_query | normal_query | normal_query |
  normal_query | normal_query | normal_query | normal_query | normal_query |
  data_dictionary_query | data_dictionary_query ;

normal_query:
# These are standard, plain queries that don't do a whole heck of a lot beyond
# doing 'something on the database'.  Mainly, we want to have something
# going on in the background when we do have a data_dictionary_query
  select_query | select_query_set |
  null_alter_query_set ;

select_query_set:
  select_query_set ; select_query | select_query ;

select_query:
# Attempt to generate a semi-hairy query that the database can chew on
# for a bit
  SELECT select_list
  FROM join_list
  WHERE where_list ;

null_alter_query_set:
# Set of queries that when, taken together results in 0 change to the table
# We do this to generate sufficient activity in the database
  alter_table_index_set | alter_table_index_set |
  alter_table_column_set ;

alter_table_index_set:
# Add + Drop an index from the table
  ALTER TABLE _table[invariant] ADD INDEX `_quid[invariant]` (small_col_list) ;  ALTER TABLE _table[invariant] DROP INDEX `_quid[invariant]` ;

alter_table_column_set:
  ALTER TABLE _table[invariant]  ADD COLUMN ( _quid[invariant] INT NOT NULL ) ;  ALTER TABLE _table[invariant]  DROP COLUMN _quid ;

data_dictionary_query:
  SHOW PROCESSLIST |
  SHOW PROCESSLIST ;

select_list:
  select_list , select_item | select_item ;

small_col_list:
  small_col_list , column_name | column_name | column_name | column_name ;

column_name:
  int_field_name | char_field_name ;

select_item:
  _field AS {"field".++$fields} ;

join_list:
  ( new_table_item join_type new_table_item ON (join_condition_item ) ) |
  ( new_table_item join_type ( ( new_table_item join_type new_table_item ON (join_condition_item ) ) ) ON (join_condition_item ) ) ;

join_type:
	INNER JOIN | left_right outer JOIN | STRAIGHT_JOIN ;  

join_condition_item:
    current_table_item . int_indexed = previous_table_item . int_field_name  |
    current_table_item . int_field_name = previous_table_item . int_indexed  |
    current_table_item . char_indexed = previous_table_item . char_field_name  |
    current_table_item . char_field_name = previous_table_item . char_indexed  ;

_table:
  CC | CC | DD | DD | BB | BB | C | D ;

new_table_item:
	_table AS { "table".++$tables } ;

current_table_item:
	{ "table".$tables };

previous_table_item:
	{ "table".($tables - 1) };

int_field_name:
    `pk` | `col_int_key` | `col_int` |
    `col_bigint` | `col_bigint_key` |
    `col_int_not_null` | `col_int_not_null_key` ;

char_field_name:
        `col_char_10` | `col_char_10_key` | `col_text_not_null` | `col_text_not_null_key` |
        `col_text_key` | `col_text` | `col_char_10_not_null_key` | `col_char_10_not_null` |
        `col_char_1024` | `col_char_1024_key` | `col_char_1024_not_null` | `col_char_1024_not_null_key` ;

char_field_name_disabled:
# need to explore enum more before enabling this
        `col_enum` | `col_enum_key` | `col_enum_not_null` | `col_enum_not_null_key` ;

int_indexed:
    `pk` | `col_int_key` | `col_bigint_key` | `col_int_not_null_key` ;

char_indexed:
    `col_char_1024_key` | `col_char_1024_not_null_key` |
    `col_char_10_key` | `col_char_10_not_null_key` ;

where_list:
  where_list and_or where_item | where_item | where_item ;

where_item:
  int_field_name comparison_operator _digit |
  char_field_name comparison_operator _char ;

and_or:
  AND | AND | AND | OR ;

comparison_operator:
  = | > | < | != | <> | <= | >= ;

left_right:
	LEFT | RIGHT ;

outer:
	| OUTER ;

