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
# outer_join.yy
# Purpose:  Random Query Generator grammar for testing larger (6 - 10 tables) JOINs
# Tuning:   Please tweak the rule table_or_joins ratio of table:join for larger joins
#           NOTE:  be aware that larger (15-20 tables) queries can take far too 
#                  long to run to be of much interest for fast, automated testing
#
# Notes:    This grammar is designed to be used with gendata=conf/optimizer/outer_join.zz
#           It can be altered, but one will likely need field names
#           Additionally, it is not recommended to use the standard RQG-produced
#           tables as they way we pick tables can result in the use of
#           several large tables that will bog down a generated query
#           
#           Please rely largely on the _portable variant of this grammar if
#           doing 3-way comparisons as it has altered code that will produce
#           more standards-compliant queries for use with other DBMS's
#  
#           We keep the grammar here as it is in order to also test certain
#           MySQL-specific syntax variants.
################################################################################


query:
  normal_query | normal_query | normal_query | normal_query | data_dictionary ;

data_dictionary:
  show_command | data_dictionary_query ;

show_command:
  SHOW PROCESSLIST | SHOW PROCESSLIST | SHOW PROCESSLIST | SHOW PROCESSLIST |
  SHOW TABLE STATUS | SHOW TABLE STATUS | SHOW TABLE STATUS |
  SHOW TABLES | SHOW TABLES | SHOW TABLES |
  SHOW SCHEMAS |
  SHOW TEMPORARY TABLES ;

data_dictionary_query:
  SELECT * FROM `data_dictionary` . data_dictionary_table ;

data_dictionary_table:
TABLES                 | 
COLLATIONS              | 
MYSQL_PROTOCOL_STATUS   | 
CURRENT_SQL_COMMANDS    | 
CHARACTER_SETS          | 
SHOW_TEMPORARY_TABLES   | 
INDEXES                 | 
INDEX_PARTS             | 
INNODB_TRX              | 
DRIZZLE_PROTOCOL_STATUS | 
COUNTER                 | 
RANDOM_NUMBER           | 
PROCESSLIST             | 
PLUGINS                 | 
SHOW_COLUMNS            | 
TABLE_CACHE             | 
INNODB_STATUS           | 
CUMULATIVE_SQL_COMMANDS | 
INNODB_CMPMEM_RESET     | 
SCOREBOARD_STATISTICS   | 
SESSION_VARIABLES       | 
INNODB_LOCKS            | 
TABLE_DEFINITION_CACHE  | 
SHOW_TABLES             | 
SCHEMAS                 | 
INNODB_CMPMEM           | 
REFERENTIAL_CONSTRAINTS | 
INNODB_INTERNAL_TABLES  | 
TABLE_CONSTRAINTS       | 
REPLICATION_STREAMS     | 
RANDOM_STRING           | 
SHOW_SCHEMAS            | 
SESSION_STATUS          | 
SHOW_TABLE_STATUS       | 
GLOBAL_VARIABLES        | 
INNODB_CMP              | 
COLUMNS                 | 
SESSION_STATEMENTS      | 
INNODB_LOCK_WAITS       | 
SHOW_INDEXES            | 
GLOBAL_STATEMENTS       | 
MODULES                 | 
INNODB_CMP_RESET        | 
GLOBAL_STATUS           | 
CUMULATIVE_USER_STATS   ; 


normal_query:
  { @nonaggregates = () ; $tables = 0 ; $fields = 0 ;  "" } query_type ;

query_type:
  simple_select | simple_select | mixed_select | mixed_select | mixed_select | aggregate_select ;

mixed_select:
        { $stack->push() } SELECT distinct straight_join select_option select_list FROM join WHERE where_list group_by_clause having_clause order_by_clause { $stack->pop(undef) } ;

simple_select:
        { $stack->push() } SELECT distinct straight_join select_option simple_select_list FROM join WHERE where_list  optional_group_by having_clause order_by_clause { $stack->pop(undef) } ;

aggregate_select:
        { $stack->push() } SELECT distinct straight_join select_option aggregate_select_list FROM join WHERE where_list optional_group_by having_clause order_by_clause { $stack->pop(undef) } ;

distinct: DISTINCT | | | |  ;

select_option:  | | | | | | | | | SQL_SMALL_RESULT ;

straight_join:  | | | | | | | | | | | STRAIGHT_JOIN ;

select_list:
	new_select_item |
	new_select_item , select_list |
        new_select_item , select_list ;

simple_select_list:
        nonaggregate_select_item |
        nonaggregate_select_item , simple_select_list |
        nonaggregate_select_item , simple_select_list ;

aggregate_select_list:
        aggregate_select_item | aggregate_select_item |
        aggregate_select_item, aggregate_select_list ;

new_select_item:
        nonaggregate_select_item |
        nonaggregate_select_item |        
        nonaggregate_select_item |
        nonaggregate_select_item |        
        nonaggregate_select_item |
	aggregate_select_item ;

nonaggregate_select_item:
        table_alias . int_field_name AS { my $f = "field".++$fields ; push @nonaggregates , $f ; $f } ;

aggregate_select_item:
        aggregate table_alias . int_field_name ) AS {"field".++$fields } ; 
	
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
   { my $left = $stack->get("left"); my %s=map{$_=>1} @$left; my @r=(keys %s); my $table_string = $prng->arrayElement(\@r); my @table_array = split(/AS/, $table_string); $table_array[1] } . int_indexed = 
   { my $right = $stack->get("result"); my %s=map{$_=>1} @$right; my @r=(keys %s); my $table_string = $prng->arrayElement(\@r); my @table_array = split(/AS/, $table_string); $table_array[1] } . int_indexed
   { my $left = $stack->get("left");  my $right = $stack->get("result"); my @n = (); push(@n,@$right); push(@n,@$left); $stack->pop(\@n); return undef } |
   { my $left = $stack->get("left"); my %s=map{$_=>1} @$left; my @r=(keys %s); my $table_string = $prng->arrayElement(\@r); my @table_array = split(/AS/, $table_string); $table_array[1] } . int_indexed =
   { my $right = $stack->get("result"); my %s=map{$_=>1} @$right; my @r=(keys %s); my $table_string = $prng->arrayElement(\@r); my @table_array = split(/AS/, $table_string); $table_array[1] } . int_field_name
   { my $left = $stack->get("left");  my $right = $stack->get("result"); my @n = (); push(@n,@$right); push(@n,@$left); $stack->pop(\@n); return undef } |
   { my $left = $stack->get("left"); my %s=map{$_=>1} @$left; my @r=(keys %s); my $table_string = $prng->arrayElement(\@r); my @table_array = split(/AS/, $table_string); $table_array[1] } . int_field_name =
   { my $right = $stack->get("result"); my %s=map{$_=>1} @$right; my @r=(keys %s); my $table_string = $prng->arrayElement(\@r); my @table_array = split(/AS/,
 $table_string); $table_array[1] } . int_indexed
   { my $left = $stack->get("left");  my $right = $stack->get("result"); my @n = (); push(@n,@$right); push(@n,@$left); $stack->pop(\@n); return undef } |
   { my $left = $stack->get("left"); my %s=map{$_=>1} @$left; my @r=(keys %s); my $table_string = $prng->arrayElement(\@r); my @table_array = split(/AS/, $table_string); $table_array[1] } . int_field_name =
   { my $right = $stack->get("result"); my %s=map{$_=>1} @$right; my @r=(keys %s); my $table_string = $prng->arrayElement(\@r); my @table_array = split(/AS/,
 $table_string); $table_array[1] } . int_field_name
   { my $left = $stack->get("left");  my $right = $stack->get("result"); my @n = (); push(@n,@$right); push(@n,@$left); $stack->pop(\@n); return undef } ;

char_condition:
   { my $left = $stack->get("left"); my %s=map{$_=>1} @$left; my @r=(keys %s); my $table_string = $prng->arrayElement(\@r); my @table_array = split(/AS/, $table_string); $table_array[1] } . char_field_name =
   { my $right = $stack->get("result"); my %s=map{$_=>1} @$right; my @r=(keys %s); my $table_string = $prng->arrayElement(\@r); my @table_array = split(/AS/, $table_string); $table_array[1] } . char_field_name 
   { my $left = $stack->get("left");  my $right = $stack->get("result"); my @n = (); push(@n,@$right); push(@n,@$left); $stack->pop(\@n); return undef } |
   { my $left = $stack->get("left"); my %s=map{$_=>1} @$left; my @r=(keys %s); my $table_string = $prng->arrayElement(\@r); my @table_array = split(/AS/, $table_string); $table_array[1] } . char_indexed  =
   { my $right = $stack->get("result"); my %s=map{$_=>1} @$right; my @r=(keys %s); my $table_string = $prng->arrayElement(\@r); my @table_array = split(/AS/, $table_string); $table_array[1] } . char_field_name 
   { my $left = $stack->get("left");  my $right = $stack->get("result"); my @n = (); push(@n,@$right); push(@n,@$left); $stack->pop(\@n); return undef } |
   { my $left = $stack->get("left"); my %s=map{$_=>1} @$left; my @r=(keys %s); my $table_string = $prng->arrayElement(\@r); my @table_array = split(/AS/, $table_string); $table_array[1] } . char_field_name =
   { my $right = $stack->get("result"); my %s=map{$_=>1} @$right; my @r=(keys %s); my $table_string = $prng->arrayElement(\@r); my @table_array = split(/AS/, $table_string); $table_array[1] } . char_indexed 
   { my $left = $stack->get("left");  my $right = $stack->get("result"); my @n = (); push(@n,@$right); push(@n,@$left); $stack->pop(\@n); return undef } ;

where_list:
        where_item | where_item |
        ( where_list and_or where_item ) ;

where_item:
        existing_table_item . `pk` comparison_operator _digit  |
        existing_table_item . `pk` comparison_operator existing_table_item . int_field_name  |
	existing_table_item . int_field_name comparison_operator _digit  |
        existing_table_item . int_field_name comparison_operator existing_table_item . int_field_name |
        existing_table_item . int_field_name IS not NULL |
        existing_table_item . int_field_name not IN (number_list) |
        existing_table_item . int_field_name  not BETWEEN _digit[invariant] AND ( _digit[invariant] + _digit );

number_list:
        _digit | number_list, _digit ;

################################################################################
# We ensure that a GROUP BY statement includes all nonaggregates.              #
# This helps to ensure the query is more useful in detecting real errors /     #
# that the query doesn't lend itself to variable result sets                   #
################################################################################
group_by_clause:
	{ scalar(@nonaggregates) > 0 ? " GROUP BY ".join (', ' , @nonaggregates ) : "" }  ;

optional_group_by:
        | | | | | | | | group_by_clause ;

having_clause:
	| HAVING having_list;

having_list:
        having_item |
        having_item |
	(having_list and_or having_item)  ;

having_item:
	existing_select_item comparison_operator _digit ;

################################################################################
# We use the total_order_by rule when using the LIMIT operator to ensure that  #
# we have a consistent result set - server1 and server2 should not differ      #
################################################################################

order_by_clause:
	|
        ORDER BY total_order_by desc limit |
	ORDER BY order_by_list ;

total_order_by:
	{ join(', ', map { "field".$_ } (1..$fields) ) };

order_by_list:
	order_by_item  |
	order_by_item  , order_by_list ;

order_by_item:
	existing_select_item desc ;

desc:
        ASC | | | | | DESC ; 

################################################################################
# We mix digit and _digit here.  We want to alter the possible values of LIMIT #
# To ensure we hit varying EXPLAIN plans, but the OFFSET can be smaller        #
################################################################################

limit:
	| | LIMIT limit_size | LIMIT limit_size OFFSET _digit;

################################################################################
# recommend 8 tables : 2 joins for smaller queries, 6:2 for larger ones
################################################################################

table_or_join:
           table | table | table | table | table | 
           table | table | table | join | join ;

table_disabled:
# We use the "AS table" bit here so we can have unique aliases if we use the same table many times
       { $stack->push(); my $x = $prng->arrayElement(\@table_set)." AS table".++$tables;  my @s=($x); $stack->pop(\@s); $x } ;


table:
# We use the "AS table" bit here so we can have unique aliases if we use the same table many times
       { $stack->push(); my $x = $prng->arrayElement($executors->[0]->tables())." AS table".++$tables;  my @s=($x); $stack->pop(\@s); $x } ;

int_field_name:
  `pk` | `col_int_key` | `col_int` ;

int_indexed:
   `pk` | `col_int_key` ;

char_field_name:
  `col_varchar_10` | 
  `col_varchar_1024_key` | 
  `col_varchar_1024` | 
  `col_varchar_10_key` ; 

char_indexed:
  `col_varchar_10_key` | `col_varchar_10_key` |
  `col_varchar_1024_key` |`col_varchar_1024_key` ;


table_alias:
  table1 | table1 | table1 | table1 | table1 | table1 | table1 | table1 | table1 | table1 |
  table2 | table2 | table2 | table2 | table2 | table2 | table2 | table2 | table2 | other_table ;

other_table:
  table3 | table3 | table3 | table3 | table3 | table4 | table4 | table5 ;

existing_table_item:
	{ "table".$prng->int(1,$tables) };

existing_select_item:
	{ "field".$prng->int(1,$fields) };

_digit:
    1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
    1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | _tinyint_unsigned ;
 

and_or:
   AND | AND | OR ;

comparison_operator:
	= | > | < | != | <> | <= | >= ;

aggregate:
	COUNT( distinct | SUM( distinct | MIN( distinct | MAX( distinct ;

not:
	| | | NOT;

left_right:
	LEFT | LEFT | LEFT | RIGHT ;

outer:
	| | | | OUTER ;

################################################################################
# We define LIMIT_rows in this fashion as LIMIT values can differ depending on      #
# how large the LIMIT is - LIMIT 2 = LIMIT 9 != LIMIT 19                       #
################################################################################

limit_size:
    1 | 2 | 10 | 100 | 1000;

