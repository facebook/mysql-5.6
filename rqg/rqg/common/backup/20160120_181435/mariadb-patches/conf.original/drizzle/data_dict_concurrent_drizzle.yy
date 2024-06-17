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

# data_dict_concurrent_drizzle.yy
# Designed to generate a heavy workload against the data_dictionary
# tables.  Also best used with threads > 1
# Can also be used with another grammar running against the same
# server to stress things
# 
# Designed to be used with --gendata=conf/drizzle/drizzle.zz

query_init:
  USE `data_dictionary` ;

query:
  data_dictionary_select | show_command ;

data_dictionary_select:
  simple_data_dictionary_select | 
  complex_data_dictionary_select ;

simple_data_dictionary_select:
  SELECT * FROM `data_dictionary` . data_dictionary_table ;

complex_data_dictionary_select:
  table_column_select ;

data_dictionary_table:
  CHARACTER_SETS | COLLATIONS  | 
 COLUMNS | CUMULATIVE_SQL_COMMANDS | CUMULATIVE_USER_STATS   | 
 CURRENT_SQL_COMMANDS | GLOBAL_STATEMENTS | GLOBAL_STATUS | 
 GLOBAL_VARIABLES | INDEXES | INDEX_PARTS | 
 INNODB_CMP  | INNODB_CMPMEM | INNODB_CMPMEM_RESET | 
 INNODB_CMP_RESET | INNODB_INTERNAL_TABLES  | INNODB_LOCKS | 
 INNODB_LOCK_WAITS | INNODB_STATUS | INNODB_TRX  | 
 MODULES | PLUGINS | PROCESSLIST | REFERENTIAL_CONSTRAINTS | 
 REPLICATION_STREAMS | SCHEMAS | SCOREBOARD_STATISTICS   | 
 SESSION_STATEMENTS | SESSION_STATUS | SESSION_VARIABLES  | 
 SHOW_COLUMNS | SHOW_INDEXES | SHOW_SCHEMAS | SHOW_TABLES | 
 SHOW_TABLE_STATUS | SHOW_TEMPORARY_TABLES   | TABLES  | 
 TABLE_CACHE | TABLE_CONSTRAINTS  | TABLE_DEFINITION_CACHE  ;

show_command:
  SHOW TABLES | SHOW TABLE STATUS | SHOW TEMPORARY TABLES |
  SHOW PROCESSLIST | SHOW PROCESSLIST  | SHOW PROCESSLIST |
  SHOW global_session STATUS | SHOW global_session VARIABLES ;

global_session:
  GLOBAL | SESSION | ;

table_column_select:
# JOIN of tables and columns tables
  SELECT table_column_select_list 
  FROM tables , columns
  WHERE tables. table_name = columns . table_name
  opt_where_list ;

table_column_select_list:
  table_column_select_list , table_column_select_item | 
  table_column_select_item ;

table_column_select_item:
 COLUMNS . TABLE_SCHEMA  | COLUMNS . TABLE_NAME | 
 COLUMNS . COLUMN_NAME | COLUMNS . COLUMN_TYPE | 
 COLUMNS . ORDINAL_POSITION | COLUMNS . COLUMN_DEFAULT | 
 COLUMNS . COLUMN_DEFAULT_IS_NULL | COLUMNS .  COLUMN_DEFAULT_UPDATE |
 COLUMNS . IS_NULLABLE | COLUMNS . IS_INDEXED | 
 COLUMNS . IS_USED_IN_PRIMARY | COLUMNS . IS_UNIQUE  | 
 COLUMNS . IS_MULTI   | COLUMNS . IS_FIRST_IN_MULTI  |
 COLUMNS . INDEXES_FOUND_IN | COLUMNS . DATA_TYPE  | 
 COLUMNS . CHARACTER_MAXIMUM_LENGTH  | COLUMNS . CHARACTER_OCTET_LENGTH |
 COLUMNS . NUMERIC_PRECISION    | COLUMNS . NUMERIC_SCALE   | 
 COLUMNS . COLLATION_NAME  | COLUMNS . COLUMN_COMMENT  |
 TABLES . TABLE_SCHEMA    | TABLES . TABLE_NAME | 
 TABLES . TABLE_TYPE | TABLES . ENGINE |
 TABLES . ROW_FORMAT | TABLES . TABLE_COLLATION |
 TABLES . TABLE_CREATION_TIME  | TABLES . TABLE_UPDATE_TIME | 
 TABLES . TABLE_COMMENT ; 

opt_where_list:
  | | AND tables_columns . name_schema comparison_operator where_value opt_where_clause ;

opt_where_clause:
  | | | | | | | | | | and_or table_column_select_item comparison_operator _value ;

tables_columns:
  TABLES | TABLES | COLUMNS ;

where_value:
  `data_dictionary` | `test` | `information_schema` | _quid ;

name_schema:
  table_schema | table_schema | table_schema | table_name ;

comparison_operator:
  > | >= | < | <= | 
  = | =  | = | != ;

and_or:
  AND | AND | OR ;
