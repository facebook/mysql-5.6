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

################################################################################
# optimizer_access.yy - RQG grammar to reliably mix a variety of table access
#                       methods (range, union, sort_union, etc)
# 
# The design goal is to have a minimum of 3 tables per query
# This needs to be implemented as the current JOIN construction rule
# still produces a number of queries with < 3 tables (and a number of invalid
# ( and a number of invalid queries as a result - SELECT table3.whatever FROM
#   table1, table2 WHERE...)
# However, the grammar will produce a varied amount of EXPLAIN output (shown via
# --debug) and is useful
#
# NOTE:  This must be used with a gendata file, I have been using 
#        range_access.zz with success for now
#        If you want to try an alternate, ensure that the rules for 
#        things like int_field, char_field, etc are correct for the gendata file
################################################################################

query:
  { @nonaggregates = () ; $tables = 0 ; $fields = 0 ; "" } main_select ;

main_select:
  simple_select | mixed_select | mixed_select ;

simple_select:
  SELECT distinct straight_join select_option simple_select_list
  FROM join
  where_clause
  optional_group_by
  having_clause
  order_by_clause ;

mixed_select:
  SELECT distinct straight_join select_option select_list
  FROM join
  where_clause
  group_by_clause
  having_clause
  order_by_clause ;

distinct: DISTINCT | | | |  ;

select_option:  | | | | | | | | | SQL_SMALL_RESULT ;

straight_join:  | | | | | | | | | | | STRAIGHT_JOIN ;

simple_select_list:
  table1_simple_select_list , table2_simple_select_list , table3_simple_select_list |
  table3_simple_select_list , table1_simple_select_list , table2_simple_select_list |
  simple_wild_list ;

simple_wild_list:
  nonaggregate_select_item |
  simple_wild_list , nonaggregate_select_item |
  simple_wild_list , nonaggregate_select_item ;

select_list:
  new_select_item |
  new_select_item , select_list |
  new_select_item , select_list ;

table1_simple_select_list:
  { $cur_table='table1'; ""} special_simple_select_list ;

table2_simple_select_list:
  { $cur_table='table2'; ""} special_simple_select_list ;

table3_simple_select_list:
  { $cur_table='table3'; ""} special_simple_select_list ;

special_simple_select_list:
# NOTE: need to determine non-indexed fields so we
#       can have a non-indexed list here
    indexed_spec_list | mixed_spec_list ;

indexed_spec_list:
  { $cur_table } . _field_indexed AS { my $f = "field".++$fields ; push @nonaggregates , $f ; $f } |
  { $cur_table } . _field_indexed AS { my $f = "field".++$fields ; push @nonaggregates , $f ; $f } , indexed_spec_list ;

mixed_spec_list:
  { $cur_table } . _field AS { my $f = "field".++$fields ; push @nonaggregates , $f ; $f } |
  { $cur_table } . _field AS { my $f = "field".++$fields ; push @nonaggregates , $f ; $f } , mixed_spec_list ;

new_select_item:
  nonaggregate_select_item |
  nonaggregate_select_item |
  nonaggregate_select_item |        
  aggregate_select_item ;

nonaggregate_select_item:
        table_123 . _field_indexed AS { my $f = "field".++$fields ; push @nonaggregates , $f ; $f } |
        table_123 . _field_indexed AS { my $f = "field".++$fields ; push @nonaggregates , $f ; $f } |
	table_123 . _field AS { my $f = "field".++$fields ; push @nonaggregates , $f ; $f } ;

aggregate_select_item:
        aggregate table_123 . aggregate_field ) AS { "field".++$fields } ; 


join:
       { $stack->push() }      
       table_or_join 
       { $stack->set("left",$stack->get("result")); }
       left_right outer JOIN table_or_join 
       ON 
       join_condition ;

join_condition:
   int_condition | char_condition ;

int_condition: 
   { my $left = $stack->get("left"); my %s=map{$_=>1} @$left; my @r=(keys %s); my $table_string = $prng->arrayElement(\@r); my @table_array = split(/AS/, $table_string); $table_array[1] } . int_indexed join_condition_operator
   { my $right = $stack->get("result"); my %s=map{$_=>1} @$right; my @r=(keys %s); my $table_string = $prng->arrayElement(\@r); my @table_array = split(/AS/, $table_string); $table_array[1] } . int_indexed
   { my $left = $stack->get("left");  my $right = $stack->get("result"); my @n = (); push(@n,@$right); push(@n,@$left); $stack->pop(\@n); return undef } |
   { my $left = $stack->get("left"); my %s=map{$_=>1} @$left; my @r=(keys %s); my $table_string = $prng->arrayElement(\@r); my @table_array = split(/AS/, $table_string); $table_array[1] } . int_indexed join_condition_operator
   { my $right = $stack->get("result"); my %s=map{$_=>1} @$right; my @r=(keys %s); my $table_string = $prng->arrayElement(\@r); my @table_array = split(/AS/, $table_string); $table_array[1] } . int_field_name
   { my $left = $stack->get("left");  my $right = $stack->get("result"); my @n = (); push(@n,@$right); push(@n,@$left); $stack->pop(\@n); return undef }  ;

char_condition:
   { my $left = $stack->get("left"); my %s=map{$_=>1} @$left; my @r=(keys %s); my $table_string = $prng->arrayElement(\@r); my @table_array = split(/AS/, $table_string); $table_array[1] } . char_field_name join_condition_operator
   { my $right = $stack->get("result"); my %s=map{$_=>1} @$right; my @r=(keys %s); my $table_string = $prng->arrayElement(\@r); my @table_array = split(/AS/, $table_string); $table_array[1] } . char_field_name 
   { my $left = $stack->get("left");  my $right = $stack->get("result"); my @n = (); push(@n,@$right); push(@n,@$left); $stack->pop(\@n); return undef } |
   { my $left = $stack->get("left"); my %s=map{$_=>1} @$left; my @r=(keys %s); my $table_string = $prng->arrayElement(\@r); my @table_array = split(/AS/, $table_string); $table_array[1] } . char_indexed  join_condition_operator
   { my $right = $stack->get("result"); my %s=map{$_=>1} @$right; my @r=(keys %s); my $table_string = $prng->arrayElement(\@r); my @table_array = split(/AS/, $table_string); $table_array[1] } . char_indexed 
   { my $left = $stack->get("left");  my $right = $stack->get("result"); my @n = (); push(@n,@$right); push(@n,@$left); $stack->pop(\@n); return undef }  ;

table_or_join:
           table | table | table | table | table | table | 
           table | join ;

table:
# We use the "AS table" bit here so we can have unique aliases if we use the same table many times
       { $stack->push(); my $x = $prng->arrayElement($executors->[0]->tables())." AS table".++$tables;  my @s=($x); $stack->pop(\@s); $x } ;

table_123:
  table1 | table1 | table2 | table2 | table3 ;

join_condition_operator:
  = | = | = | = | = | = | = | < | <= | > | >= ;

where_clause:
  WHERE t1_where_list and_or t2_where_list and_or t3_where_list |
  WHERE t1_where_list and_or t2_where_list and_or t3_where_list |
  WHERE t1_where_list and_or t2_where_list  ;
 

t1_where_list:
  { $cur_table='table1'; "" } targeted_where_list ;

t2_where_list:
  { $cur_table='table1'; "" } targeted_where_list ;  

t3_where_list:
  { $cur_table='table1'; "" } targeted_where_list ;

targeted_where_list:
   range_where_list | sort_union_where_list | union_where_list ;

range_where_list:
  range_where_item and_or range_where_item |
  range_where_list and_or range_where_item |
  range_where_item ;

range_where_item:
  { $cur_table } . int_field_name comparison_operator int_value |
  { $cur_table } . int_field_name comparison_operator existing_table_item . int_field_name |
  { $cur_table } . int_field_name IS not NULL |
  { $cur_table } . int_field_name not IN ( number_list ) |
  { $cur_table } . int_field_name not BETWEEN _digit[invariant] AND ( _digit[invariant] + increment ) |
  { $cur_table } . char_field_name comparison_operator char_value |
  { $cur_table } . char_field_name comparison_operator existing_table_item . char_field_name |
  { $cur_table } . char_field_name not IN ( char_list ) |
  { $cur_table } . char_field_name not BETWEEN _char[invariant] AND CHAR(ASCII( _char[invariant] ) + increment ) |
  { $cur_table } . char_field_name IS not NULL ;

sort_union_where_list:
  int_idx_where_clause | char_idx_where_clause |
  sort_union_where_list OR int_idx_where_clause |
  sort_union_where_list OR char_idx_where_clause ;

int_idx_where_clause:
   { my @int_idx_fields = ("`pk`" , "`col_int_key`") ; $int_idx_field = $cur_table." . ".$prng->arrayElement(\@int_idx_fields) ; "" } single_int_idx_where_list ;

single_int_idx_where_list:
   single_int_idx_where_list and_or single_int_idx_where_item |
   single_int_idx_where_item | single_int_idx_where_item ;

single_int_idx_where_item:
   { $int_idx_field } greater_than _digit[invariant] AND { $int_idx_field } less_than ( _digit[invariant] + increment ) |
   { $int_idx_field } greater_than _digit[invariant] AND { $int_idx_field } less_than ( _digit[invariant] + increment ) |
   { $int_idx_field } comparison_operator existing_table_item . int_field_name |
   { $int_idx_field } greater_than _digit[invariant] AND { $int_idx_field } less_than ( _digit + int_value ) |
   { $int_idx_field } greater_than _digit AND { $int_idx_field } less_than ( _digit + increment ) |
   { $int_idx_field } comparison_operator int_value |
   { $int_idx_field } not_equal int_value |
   { $int_idx_field } not IN ( number_list ) |
   { $int_idx_field } not BETWEEN _digit[invariant] AND ( _digit[invariant] + int_value ) |
   { $int_idx_field } IS not NULL ;

char_idx_where_clause:
  { my @char_idx_fields = ("`col_varchar_10_latin1_key`", "`col_varchar_10_utf8_key`", "`col_varchar_1024_latin1_key`", "`col_varchar_1024_utf8_key`") ; $char_idx_field = $cur_table." . ".$prng->arrayElement(\@char_idx_fields) ; "" } single_char_idx_where_list ;

single_char_idx_where_list:
  single_char_idx_where_list and_or single_char_idx_where_item |
  single_char_idx_where_item | single_char_idx_where_item ;

single_char_idx_where_item:
  { $char_idx_field } greater_than _char AND { $char_idx_field } less_than 'z' |
  { $char_idx_field } greater_than _char AND { $char_idx_field } less_than 'z' |
  { $char_idx_field } comparison_operator existing_table_item . char_field_name |
  { $char_idx_field } greater_than char_value AND { $char_idx_field } less_than char_value |
  { $char_idx_field } greater_than char_value AND { $char_idx_field } less_than 'zzzz' |
  { $char_idx_field } IS not NULL |
  { $char_idx_field } not IN (char_list) |
  { $char_idx_field } not LIKE ( char_pattern ) |
  { $char_idx_field } not BETWEEN _char AND 'z' ;

union_where_list:
  int_idx_where_clause | char_idx_where_clause |
  union_where_list OR spec_int_idx_where_clause |
  union_where_list OR spec_char_idx_where_clause ;

spec_int_idx_where_clause:
   { my @int_idx_fields = ("`pk`" , "`col_int_key`") ; $int_idx_field = $cur_table." . ".$prng->arrayElement(\@int_idx_fields) ; "" } spec_single_int_idx_where_list ;

spec_single_int_idx_where_list:
   spec_single_int_idx_where_list and_or spec_single_int_idx_where_item |
   spec_single_int_idx_where_item | spec_single_int_idx_where_item ;

spec_single_int_idx_where_item:
   { $int_idx_field } >= _digit[invariant] AND { $int_idx_field } <= ( _digit[invariant] + increment ) |
   { $int_idx_field } >= _digit[invariant] AND { $int_idx_field } <= ( _digit[invariant] + increment ) |
   { $int_idx_field } >= _digit AND { $int_idx_field } <= ( _digit[invariant] + int_value ) |
   { $int_idx_field } = int_value |
   { $int_idx_field } not IN ( number_list ) |
   { $int_idx_field } not BETWEEN _digit[invariant] AND ( _digit[invariant] + int_value ) |
   { $int_idx_field } IS not NULL ;

spec_char_idx_where_clause:
  { my @char_idx_fields = ("`col_varchar_10_latin1_key`", "`col_varchar_10_utf8_key`", "`col_varchar_1024_latin1_key`", "`col_varchar_1024_utf8_key`") ; $char_idx_field = $cur_table." . ".$prng->arrayElement(\@char_idx_fields) ; "" } spec_single_char_idx_where_list ;

spec_single_char_idx_where_list:
  spec_single_char_idx_where_list and_or spec_single_char_idx_where_item |
  spec_single_char_idx_where_item | spec_single_char_idx_where_item ;

spec_single_char_idx_where_item:
  { $char_idx_field } >= low_char AND { $char_idx_field } <= high_char |
  { $char_idx_field } >= low_char AND { $char_idx_field } <= high_char |
  { $char_idx_field } >= _char AND { $char_idx_field } <= high_char |
  { $char_idx_field } >= char_value AND { $char_idx_field } <= char_value |
  { $char_idx_field } >= char_value AND { $char_idx_field } <= high_char |
  { $char_idx_field } IS not NULL |
  { $char_idx_field } not IN (char_list) |
  { $char_idx_field } not LIKE ( char_pattern ) |
  { $char_idx_field } not BETWEEN _char AND 'z' ;


################################################################################
# We ensure that a GROUP BY statement includes all nonaggregates.              #
# This helps to ensure the query is more useful in detecting real errors /     #
# that the query doesn't lend itself to variable result sets                   #
################################################################################
group_by_clause:
	{ scalar(@nonaggregates) > 0 ? " GROUP BY ".join (', ' , @nonaggregates ) : "" }  ;

optional_group_by:
        | | group_by_clause ;

having_clause:
	| | | | HAVING having_list;

having_list:
        having_item |
        having_item |
	(having_list and_or having_item)  ;

having_item:
	existing_table_item . int_field_name comparison_operator int_value |
        existing_table_item . char_field_name comparison_operator char_value ;

################################################################################
# We use the total_order_by rule when using the LIMIT operator to ensure that  #
# we have a consistent result set - server1 and server2 should not differ      #
################################################################################

order_by_clause:
	|
        ORDER BY total_order_by desc limit ;

total_order_by:
	{ join(', ', map { "field".$_ } (1..$fields) ) };

desc:
        ASC | | | | | DESC ; 

################################################################################
# We define LIMIT_rows in this fashion as LIMIT values can differ depending on #
# how large the LIMIT is - LIMIT 2 = LIMIT 9 != LIMIT 19                       #
################################################################################

limit:
  | | | | | LIMIT limit_value ;

limit_value:
    1 | 2 | 10 | 100 | 1000 ;

aggregate:
	COUNT( | SUM( | MIN( | MAX( ;

number_list:
        int_value | number_list, int_value ;

char_list: 
        char_value | char_list, char_value ;

left_right:
	LEFT | LEFT | LEFT | RIGHT ;

outer:
	| | | | OUTER ;

and_or:
  AND | AND | OR ;

or_and:
  OR | OR | AND ;

not:
  | | | | NOT ;

comparison_operator:
  = | > | < | != | <> | <= | >= ;

greater_than:
  > | >= ;

less_than:
  < | <= ;

not_equal:
  <> | != ;

increment:
  1 | 1 | 2 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 20 | 25 | 100 ;

int_value:
  _digit | increment | limit_value ;

char_value:
  _char | _quid | _english ;

low_char:
 'a' | 'b' | 'c' | 'd' | 
 'a' | 'b' | 'c' | 'd' |
 'h' | 'i' | 'j' ; 

high_char:
 'w' | 'x' | 'y' | 'z' |
 'w' | 'x' | 'y' | 'z' | 
 'p' | 'r' | 't' ;


char_pattern:
  char_value | char_value | CONCAT( _char, '%') | 'a%' | _quid | '_' | '_%' ;

existing_table_item:
  { "table".$prng->int(1,$tables) } ;

int_indexed:
   `pk` | `col_int_key` ;

int_field_name: 
   `pk` | `col_int_key` | `col_int` ;

char_indexed:  
   `col_varchar_10_latin1_key` | `col_varchar_10_utf8_key` | 
   `col_varchar_1024_latin1_key` | `col_varchar_1024_utf8_key`;
 
char_field_name:
   `col_varchar_10_latin1_key` | `col_varchar_10_utf8_key` | 
   `col_varchar_1024_latin1_key` | `col_varchar_1024_utf8_key` |
   `col_varchar_10_latin1` | `col_varchar_10_utf8` | 
   `col_varchar_1024_latin1` | `col_varchar_1024_utf8` ; 

date_field_name:
  `col_date_key` | `col_date` ;

aggregate_field:
  int_field_name | char_field_name | int_field_name | char_field_name | 
  int_field_name | char_field_name | int_field_name | char_field_name | 
  int_field_name | char_field_name | int_field_name | char_field_name |
  date_field_name ;



