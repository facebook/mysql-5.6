# Copyright (C) 2010 Patrick Crews. All rights reserved.
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
# drizzledump_migrate.yy
# grammar for generating test beds for testing drizzledump's ability
# to assist with migration from MySQL databases
# This grammar is intended to be run against a MySQL server, the 
# accompanying Validator will attempt to migrate the MySQL data 
# to a Drizzle validation server when signalled. 
#
# TODO:  -Add ability to CREATE/DROP/populate additional databases
#        -Creation of Foreign Keys
#        -other deviltry as it comes to mind
#        -Update grammar for use with other data type columns (mediumint, etc)
#        -

query:
  { $tables = 0 ;  "" } 
  DROP DATABASE IF EXISTS drizzledump_db ;  CREATE DATABASE drizzledump_db ;  USE drizzledump_db ; create_test_table_list ; SELECT 1 ;

create_test_table_list:
# rule for picking one or more tables from the initial test bed
# sub-rules handle table population and composition
 create_test_table_list ; create_test_table_set |
 create_test_table_list ; create_test_table_set |
 create_test_table_set ; create_test_table_set ;
# create_test_table_set ; create_test_table_set ; create_test_table_set ; create_test_table_set ; create_test_table_set ; create_test_table_set ; create_test_table_set ; 

create_test_table_set:
  select_set | like_set ;

like_set:
  create_table_like ; populate_table ; modify_table ; 

select_set:
  create_table_select ; modify_table2 ;

create_table_like:
# even though all test tables have the same columns, they are created in
# different orders, so we randomly choose a table from the test db
# to enhance randomness / alter the composition of our test tables
  CREATE TABLE new_test_table LIKE `test` . _table ;

populate_table:
# We fill the test table with rows SELECT'ed from the
# existing tables in the test db
  insert_query_list ;

insert_query_list:
  insert_query_list ; insert_query | insert_query | insert_query ;

insert_query:
# We work on one test table at a time, thus we use the $tables variable to let us
# reference the current table (started in the create_table rule) here
  INSERT INTO {"dump_table".$tables } ( insert_column_list ) SELECT insert_column_list FROM `test` . _table insert_where_clause LIMIT small_digit;

insert_where_clause:
# we use a WHERE clause on the populating SELECT to increase randomness
  | ;

insert_column_list:
# We use a set column list because even though all tables have the same
# columns, each table has a different order of those columns for 
# enhanced randomness
`col_mediumint`, `col_mediumint_not_null_key`,  `col_mediumint_key`, `col_mediumint_not_null`,
`col_char_128_not_null_key`, `col_char_128_not_null`, `col_char_128_key`, `col_char_128`,
`col_smallint_key`, `col_smallint_not_null`, `col_smallint_not_null_key`, `col_smallint`,
`col_bigint_not_null`, `col_bigint`, `col_bigint_key`, `col_bigint_not_null_key`,
`col_enum_key`, `col_enum_not_null_key`, `col_enum_not_null`,  `col_enum`,
`col_char_10`,  `col_char_10_not_null`, `col_char_10_key`, `col_char_10_not_null_key`,
`col_int_key`,    `col_int`,  `col_int_not_null`, `col_int_not_null_key`,    
`col_tinyint_not_null_key`, `col_tinyint_not_null`, `col_tinyint`, `col_tinyint_key` ,
`col_timestamp_not_null`, `col_timestamp_not_null_key`, `col_timestamp`, `col_timestamp_key`,
`col_datetime`, `col_datetime_key`, `col_datetime_not_null`, `col_datetime_not_null_key` ,
`col_year`, `col_year_key`, `col_year_not_null`, `col_year_not_null_key`,
`col_time`, `col_time_key`, `col_time_not_null_key`, `col_time_not_null` ,
`col_binary_5`, `col_binary_5_key`, `col_binary_5_not_null`, `col_binary_5_not_null_key`,
`col_varbinary_5`, `col_varbinary_5_key`, `col_varbinary_5_not_null`, #`col_varbinary_5_not_null_key` ,
`col_text`, `col_text_key`, `col_text_not_null_key`, `col_text_not_null` ;

modify_table:
# We alter the tables by ALTERing the table and DROPping COLUMNS
# we also include not dropping any columns as an option
# TODO:  Allow for adding columns
# We set the list of droppable columns here so it'll be consistent
# during the query generation
# NOTE - we don't drop pk as our comparison function relies
# on the presence of a primary key (this is a bit of a cheat, perhaps)
#
# We are currently generating individual ALTER / DROP statements.
# While it would be nice to also generate compound / multi-DROP ALTER statements,
# we would need to ensure that we don't generate bad ALTER's otherwise we
# would have a lot of bad queries / not generate as many interesting
# table combos
alter_table_list ;

alter_table_list:
    alter_table_list ; alter_table_item | 
    alter_table_list ; alter_table_item |
    alter_table_list ; alter_table_item |
    alter_table_item ; alter_table_item ; alter_table_item | 
    alter_table_item ; alter_table_item ; alter_table_item ; alter_table_item ;

alter_table_item:
  ALTER TABLE { "dump_table".$tables } DROP drop_column_name ;

drop_column_name:
  `col_char_10` | `col_char_10_key` | `col_char_10_not_null` | `col_char_10_not_null_key` |
  `col_char_128` | `col_char_128_key`  | `col_char_128_not_null` | `col_char_128_not_null_key` |
  `col_int` | `col_int_key` | `col_int_not_null` | `col_int_not_null_key` |  
  `col_bigint` | `col_bigint_key` | `col_bigint_not_null` | `col_bigint_not_null_key` |  
  `col_enum` | `col_enum_key` | `col_enum_not_null` | `col_enum_not_null_key` |  
  `col_text` | `col_text_key` | `col_text_not_null` | `col_text_not_null_key` ;

create_table_select:
# alternate test-table generation rule that uses CREATE TABLE...SELECT
# for generating tables and data
  CREATE TABLE new_test_table ( `pk` INT NOT NULL AUTO_INCREMENT, PRIMARY KEY(`pk`)) select_statement ;

select_statement:
# We have to alias each column in the column list, so we limit ourselves to a 2-table join
# for simplicity.  We can perhaps look into more complex joins later
  SELECT column_list FROM `test`. _table AS t1 , `test`. _table AS t2 ;

column_list:
  t1 . `col_char_10` AS field0 ,
  t1 . `col_char_10_key` AS field1 ,
  t1 . `col_char_10_not_null` AS field2 ,
  t1 . `col_char_10_not_null_key` AS field3 ,
  t1 . `col_char_128` AS field4 ,
  t1 . `col_char_128_key` AS field5 ,
  t1 . `col_char_128_not_null` AS field6 ,
  t1 . `col_char_128_not_null_key` AS field7 ,
  t1 . `col_int` AS field8 ,
  t1 . `col_int_key` AS field9 ,
  t1 . `col_int_not_null` AS field10 ,
  t1 . `col_int_not_null_key` AS field11 ,
  t1 . `col_bigint` AS field12 ,
  t1 . `col_bigint_key` AS field13 ,
  t1 . `col_bigint_not_null` AS field14 ,
  t1 . `col_bigint_not_null_key` AS field15 ,
  t1 . `col_enum` AS field16 ,
  t1 . `col_enum_key` AS field17 ,
  t1 . `col_enum_not_null` AS field18 ,
  t1 . `col_enum_not_null_key` AS field19 ,
  t1 . `col_text` AS field20 ,
  t1 . `col_text_key` AS field21 ,
  t1 . `col_text_not_null` AS field22 ,
  t1 . `col_text_not_null_key` AS field23 ,
  t2 . `col_char_10` AS field24 ,
  t2 . `col_char_10_key` AS field25 ,
  t2 . `col_char_10_not_null` AS field26 ,
  t2 . `col_char_10_not_null_key` AS field27 ,
  t2 . `col_int` AS field32 ,
  t2 . `col_int_key` AS field33 ,
  t2 . `col_int_not_null` AS field34 ,
  t2 . `col_int_not_null_key` AS field35 ,
  t2 . `col_enum` AS field40 ,
  t2 . `col_enum_key` AS field41 ,
  t2 . `col_enum_not_null` AS field42 ,
  t2 . `col_enum_not_null_key` AS field43 ;


modify_table2:
  alter_table_list2 ;

alter_table_list2:
   alter_table_list2 ; alter_table_item2 | alter_table_item2 ; 

alter_table_item2:
  ALTER TABLE { "dump_table".$tables } DROP { "field".$prng->int(0,47)  } ;

 
new_test_table:
# This rule should generate tables to be dumped named dump_table1, dump_table2, etc
  { "dump_table".++$tables } ;


 _table:
# we hack the _table rule a bit here to ensure we have a majority of populated tables being used
  AA | AA | BB | BB | 
  CC | CC | DD | DD |
  small_table ;

small_table:
  A | B |
  C | C | C | C |
  D | D | D | D ; 

small_digit:
1 ;
