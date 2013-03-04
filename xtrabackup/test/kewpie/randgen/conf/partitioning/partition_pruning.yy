# Copyright (C) 2008-2010 Sun Microsystems, Inc. All rights reserved.
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

# **NOTE** Joins for this grammar are currently not working as intended.
# For example, if we have tables 1, 2, and 3, we end up with ON conditions that 
# only involve tables 2 and 3.
# This will be fixed, but initial attempts at altering this had a negative 
# impact on the coverage the test was providing.  To be fixed when scheduling 
# permits.  We are still seeing significant coverage with the grammar as-is.

################################################################################
# partition_pruning.yy
#
# Purpose:  RQG grammar for testing partitioning by COLUMN_LIST
#           , introduced in WL#3352
#
# NOTES:    * Must be used with conf/partitioning/partition_pruning.zz as the gendata
#             configuration file
#           * Designed for:
#             ** server-server comparisons, particularly
#                comparing a version of MySQL without partitioning
#                (like 5.0) to one that has partition_pruning
#             ** valgrind runs
#             ** Single-server stress / endurance testing
#            
#           * Will NOT work if the server has partitioning, but
#             not partitioning by COLUMN_LIST and
#           * Basically, the grammar was designed to ensure
#             that no result-set differences occur via the 
#             server-server compare and to look for crashes
#             and memory leaks
#           * the gendata file, conf/partitioning/partition_pruning.zz
#             contains the table setups / partitioning schemes
################################################################################


query:
  { @nonaggregates = () ; $tables = 0 ; $fields = 0 ; "" } query_type ;

################################################################################
# We use a mix of SELECT, UPDATE, INSERT, and DELETE to ensure data consistency
################################################################################

query_type:
  select | select | select | select | select |
  select | select | select | select | select |
  select | select | select | select | select |
  update | update | update | delete | delete |
  insert | insert ;

distict:  DISTINCT | | | | | | | | | ;

select_option: SQL_SMALL_RESULT | | | | | | | | | | | | | | | | | | | | ;

explain_extended:
    | | | | | | | | | explain_extended2 ;

explain_extended2: | | | | EXPLAIN | EXPLAIN EXTENDED ;


select:
  simple_select | mixed_select ;

simple_select:
  explain_extended SELECT simple_select_list
  FROM join_list
  WHERE where_list 
  optional_group_by
  having_clause
  order_by_clause ;


mixed_select:
  explain_extended SELECT select_list
  FROM join_list
  WHERE where_list
  group_by_clause
  having_clause
  order_by_clause ;

################################################################################
# For every UPDATE, INSERT and DELETE, we first do the action (UPDATE, etc)
# *then* we do a SELECT COUNT(*), looking for those rows that should have been
# touched by the previous query - most useful in server-server comparisons
################################################################################

update:
  int_update | char_update ;

int_update:
 int_update_query ; int_select_count ;

int_update_query:
  UPDATE _table[invariant] SET `col_int_signed` = _digit[invariant] WHERE special_where_list ; 

int_select_count:
  SELECT COUNT(*) FROM _table[invariant] WHERE `col_int_signed` = _digit[invariant];

char_update:
  utf8_char_update | utf8_char_update2 | utf8_char_update3 ;

utf8_char_update:
  utf8_char_update_query ; utf8_char_select_count ;

utf8_char_update_query:
  UPDATE _table[invariant] SET `col_varchar_256_utf8` = _char[invariant] WHERE special_where_list;

utf8_char_select_count:
  SELECT COUNT(*) FROM _table[invariant] WHERE `col_varchar_256_utf8` = _char[invariant];

utf8_char_update2:
  utf8_char_update_query2 ; utf8_char_select_count2 ;

utf8_char_update_query2:
  UPDATE _table[invariant] SET `col_varchar_512_utf8` = _char[invariant] WHERE special_where_list;

utf8_char_select_count2:
  SELECT COUNT(*) FROM _table[invariant] WHERE `col_varchar_512_utf8` = _char[invariant];

utf8_char_update3:
  utf8_char_update_query3 ; utf8_char_select_count3 ;

utf8_char_update_query3:
  UPDATE _table[invariant] SET `col_varchar_5_utf8` = _char[invariant] WHERE special_where_list;

utf8_char_select_count3:
  SELECT COUNT(*) FROM _table[invariant] WHERE `col_varchar_5_utf8` = _char[invariant];


delete:
  int_delete ;

int_delete:
  int_delete_query ; int_select_count ;

int_delete_query:
  DELETE FROM _table[invariant] WHERE `col_int_signed` = _digit[invariant] AND special_where_list ;

special_where_list:
  not ( special_where_item ) | not ( special_where_item ) |
  ( special_where_list and_or special_where_item ) ;

special_where_item:
  _table[invariant] . partitioned_int_field comparison_operator _digit  |
  _table[invariant] . int_field comparison_operator _digit  |
  _table[invariant] . partitioned_int_field not BETWEEN _digit[invariant] AND ( _digit[invariant] + _digit ) |
  _table[invariant] . int_field not BETWEEN _digit[invariant] AND ( _digit[invariant] + _digit ) |
  _table[invariant] . partitioned_int_field not IN ( number_list ) |
  _table[invariant] . int_field not in (number_list) |
  _table[invariant] . partitioned_char_field comparison_operator _char |
  _table[invariant] . partitioned_char_field not IN (char_list ) |
  _table[invariant] . char_field not IN (char_list) ; 

insert:
  INSERT INTO _table SELECT _field_list FROM _table[invariant] WHERE special_where_list ORDER BY _field_list LIMIT _digit ;

#######################################################
# query clause rules
#######################################################

select_list:
   new_select_item | new_select_item | new_select_item |
   new_select_item, select_list | new_select_item, select_list ;

simple_select_list:
   nonaggregate_select_item | nonaggregate_select_item | nonaggregate_select_item |
   nonaggregate_select_item | nonaggregate_select_item, simple_select_list | nonaggregate_select_item, simple_select_list ;

join_list_disabled:
   ( new_table_item join_type ( ( new_table_item join_type new_table_item ON ( join_condition ) ) ) ON ( join_condition ) ) ;

join_list:
   new_table_item join_type new_table_item ON ( join_condition ) |
   new_table_item | new_table_item | new_table_item | new_table_item | new_table_item  ;

join_type:
   INNER JOIN | left_right outer JOIN | STRAIGHT_JOIN ;

join_condition:
   current_table_item . int_indexed = previous_table_item . int_indexed |
   current_table_item . char_indexed = previous_table_item . char_indexed ; 


#########################################################
# We use partition pruning friendly clauses here
#########################################################

where_list:
  not ( where_item ) | not ( where_item ) |
  not ( where_list and_or where_item ) ;

where_item:
  table1 . partitioned_int_field comparison_operator existing_table_item . int_field  |
  table1 . partitioned_int_field comparison_operator _digit  |
  table1 . partitioned_int_field not BETWEEN _digit[invariant] AND ( _digit[invariant] + _digit ) |
  table1 . partitioned_int_field not IN ( number_list ) |
  table1 . partitioned_char_field comparison_operator _char  |
  table1 . partitioned_char_field not IN (char_list ) | 
  table1 . utf8_char_field comparison_operator existing_table_item . utf8_char_field  |
  table1 . latin1_char_field comparison_operator existing_table_item . latin1_char_field  |
  table1 . cp932_char_field comparison_operator existing_table_item . cp932_char_field  | 
  table1 . `col_date` comparison_operator _date  |
  table1 . `col_datetime` comparison_operator _datetime  |
  table1 . date_field comparison_operator _date  |
  table1 . char_field comparison_operator _char  |
  table1 . int_field  comparison_operator _digit  ;

partitioned_int_field:
    `col_int_signed` ;

partitioned_char_field:
    `col_varchar_5_utf8`    | `col_varchar_5_cp932`    | `col_varchar_5_latin1`    |
    `col_varchar_256_utf8` | `col_varchar_256_cp932` | `col_varchar_256_latin1` |
    `col_varchar_512_utf8` | `col_varchar_512_cp932` | `col_varchar_512_latin1` ;

int_field:
    `col_int_signed` | `col_int_signed_key` ;

utf8_char_field:
  `col_varchar_5_utf8` | `col_varchar_5_utf8_key` | `col_varchar_256_utf8` | `col_varchar_256_utf8_key` | `col_varchar_512_utf8` | `col_varchar_512_utf8_key` ;

latin1_char_field:
  `col_varchar_5_latin1`  | `col_varchar_5_latin1_key` | `col_varchar_256_latin1` | `col_varchar_256_latin1_key` | `col_varchar_512_latin1` | `col_varchar_512_latin1_key`;

cp932_char_field:
  `col_varchar_5_cp932` | `col_varchar_5_cp932_key` | `col_varchar_256_cp932` | `col_varchar_256_cp932_key` | `col_varchar_512_cp932` | `col_varchar_512_cp932_key` ; 

char_field:
  utf8_char_field | latin1_char_field | cp932_char_field ;

date_field:
  `col_datetime` | `col_date_key` | `col_datetime_key` | `col_date` ; 

non_int_field:
  char_field | date_field ;

number_list:
        _digit | number_list, _digit ;

char_list: 
        char | char_list, char ;

char:
  _char | _char | _char | _char | _char |
  _char | _char | _char | _char | big_char ;

big_char:
   _varchar(512) | _varchar(1024) ;

#########################################################
# GROUP BY / HAVING / ORDER BY rules
#########################################################
group_by_clause:
        { scalar(@nonaggregates) > 0 ? " GROUP BY ".join (', ' , @nonaggregates ) : "" }  ;

optional_group_by:
        | | | | | | | | | group_by_clause ;

having_clause:
        | | | | | | | | | HAVING having_list;

having_list:
        not ( having_item ) |
        not ( having_item ) |
        (having_list and_or having_item)  ;

having_item:
         existing_table_item . char_field comparison_operator _char |
         existing_table_item . int_field comparison_operator _digit |
         existing_table_item . date_field comparison_operator _date |
         existing_table_item . char_field comparison_operator existing_table_item . char_field |
         existing_table_item . int_field comparison_operator existing_table_item . int_field |
         existing_table_item . date_field comparison_operator existing_table_tiem . date_field ;

order_by_clause:
 | | | | ORDER BY total_order_by desc limit ;

total_order_by:
        { join(', ', map { "field".$_ } (1..$fields) ) };

desc:
  ASC | | | | DESC ;

limit:
  | | | | | | | | | | LIMIT limit_size | LIMIT limit_size OFFSET _digit;

limit_size:
    1 | 2 | 10 | 100 | 1000;

#########################################################
# query component rules
#########################################################

new_select_item:
  nonaggregate_select_item  | aggregate_select_item ;

nonaggregate_select_item:
  table_one_two . _field AS { my $f = "field".++$fields ; push @nonaggregates , $f ; $f } ;

aggregate_select_item:
  aggregate table_one_two . non_int_field ) AS { "field".++$fields } |
  int_aggregate table_one_two . int_field ) AS { "field".++$fields } ;


new_table_item:
	_table AS { "table".++$tables };

table_one_two:
      table1 ;

# disabled this to reduce number of bad queries
# can merge with rule table_one_two to add more
# variety to queries, but it is recommended that   
# table1 is still favored disproportionately
# (there aren't a lot of joins)
table_one_two_disabled:
 table2 ;

current_table_item:
	{ "table".$tables };

previous_table_item:
	{ "table".($tables - 1) };

existing_table_item:
	{ "table".$prng->int(1,$tables) };

left_right:
        LEFT | RIGHT ;

outer: 
        | | | OUTER ;

existing_select_item:
	{ "field".$prng->int(1,$fields) };

int_indexed:
    `col_int_signed_key` ;

char_indexed:
    `col_varchar_256_utf8_key` | `col_varchar_512_utf8_key` |
    `col_varchar_256_latin1_key` | `col_varchar_512_latin1_key` |
    `col_varchar_256_cp932_key` | `col_varchar_512_cp932_key` ;

comparison_operator:
	= | > | < | != | <> | <= | >= ;

aggregate:
        COUNT( distinct | MIN( distinct | MAX( distinct ;

int_aggregate:
        SUM( | aggregate ;

and_or:
        AND | AND | AND | AND | OR | OR | XOR ;

not:
   | | | | NOT ;


value:
        _digit | _digit | _digit | _digit | _digit |
        _char | _char | _char | _char | _char ;

