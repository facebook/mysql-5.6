# Copyright (c) 2008, 2011 Oracle and/or its affiliates. All rights reserved.
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
# optimizer_no_subquery.yy:  Random Query Generator grammar for testing        #
#                            non-subquery optimizations.  This grammar         #
#                            *should* hit the optimizations listed here:       #
#                    https://inside.mysql.com/wiki/Optimizer_grammar_worksheet #
# see:  WL#5006 Random Query Generator testing of Azalea Optimizer- subqueries #
#       https://intranet.mysql.com/worklog/QA-Sprint/?tid=5006                 #
#                                                                              #
# recommendations:                                                             #
#       queries: 10k+.  We can see a lot with lower values, but over 10k is    #
#                best.  The intersect optimization happens with low frequency  #
#                so larger values help us to hit it at least some of the time  #
#       engines: MyISAM *and* Innodb.  Certain optimizations are only hit with #
#                one engine or another and we should use both to ensure we     #
#                are getting maximum coverage                                  #
#       Validators:  ResultsetComparatorSimplify                               #
#                      - used on server-server comparisons                     #
#                    Transformer - used on a single server                     #
#                      - creates equivalent versions of a single query         # 
#                    SelectStability - used on a single server                 #
#                      - ensures the same query produces stable result sets    #
################################################################################

################################################################################
# The perl code in {} helps us with bookkeeping for writing more sensible      #
# queries.  We need to keep track of these items to ensure we get interesting  #
# and stable queries that find bugs rather than wondering if our query is      #
# dodgy.                                                                       #
################################################################################
query:
	{ @nonaggregates = () ; $tables = 0 ; $fields = 0 ; "" } query_type ;


query_type:
        main_select | main_select | main_select |  loose_scan ;

################################################################################
# The loose* rules listed below are to hit the 'Using index for group-by'       #
# optimization.  This optimization has some strict requirements, thus          #
# we needed a separate query pattern to ensure we hit it.                      #
################################################################################
loose_scan:
        SELECT distinct loose_select_clause
        FROM new_table_item
        WHERE generic_where_list;
#        group_by_clause ;

loose_select_clause:
        loose_select_list ;
#        MIN( _field_indexed) AS { "field".++$fields } , loose_select_list |
#        MAX( _field_indexed) AS { "field".++$fields } , loose_select_list |
#        MIN( _field_indexed[invariant] ) AS { "field".++$fields }, MAX( _field_indexed[invariant] ) AS { "field".++$fields }, loose_select_list ;

loose_select_list:
        loose_select_item | 
        loose_select_item , loose_select_list ;

loose_select_item:
        _field AS { my $f = "field".++$fields ; push @nonaggregates , $f ; $f } ;                
        
################################################################################
# The bulk of interesting things happen with this main rule                    #
################################################################################
main_select:
	SELECT distinct select_option select_list
	FROM join_list
	where_clause
#	group_by_clause
#        having_clause
	order_by_clause ;

distinct: ;
# DISTINCT | | | |  ;

select_option:  | | | | | | | | | SQL_SMALL_RESULT ;

select_list:
	new_select_item |
	new_select_item , select_list |
        new_select_item , select_list ;

join_list:
################################################################################
# this limits us to 2 and 3 table joins / can use it if we hit                 #
# too many mega-join conditions which take too long to run                     #
################################################################################
	( new_table_item join_type new_table_item ON (join_condition_list ) ) |
        ( new_table_item join_type ( ( new_table_item join_type new_table_item ON (join_condition_list ) ) ) ON (join_condition_list ) ) ;

join_list_disabled:
################################################################################
# preventing deep join nesting for run time / table access methods are more    #
# important here - join.yy can provide deeper join coverage                    #
# Enabling this / swapping out with join_list above can produce some           #
# time-consuming queries.                                                      #
################################################################################

        new_table_item |
        ( new_table_item join_type join_list ON (join_condition_list ) ) ;

join_type:
	INNER JOIN | left_right outer JOIN | STRAIGHT_JOIN ;  

join_condition_list:
    join_condition_item | 
    ( join_condition_item ) and_or ( join_condition_item ) |
    ( current_table_item  .`pk` arithmetic_operator previous_table_item . int_field_name ) AND (current_table_item  .`pk` arithmetic_operator previous_table_item . int_field_name ) ;    
join_condition_item:
     current_table_item . int_indexed = previous_table_item . int_field_name  |
     current_table_item . int_field_name = previous_table_item . int_indexed  |
     current_table_item . `col_varchar_key` = previous_table_item . char_field_name |
     current_table_item . char_field_name = previous_table_item . `col_varchar_key` |
     current_table_item . int_indexed arithmetic_operator previous_table_item . int_field_name  |
     current_table_item . int_field_name arithmetic_operator previous_table_item . int_indexed  |
     current_table_item . `col_varchar_key` arithmetic_operator previous_table_item . char_field_name |
     current_table_item . char_field_name arithmetic_operator previous_table_item . `col_varchar_key`;


left_right:
	LEFT | RIGHT ;

outer:
	| OUTER ;

where_clause:
        | WHERE where_list ;


where_list:
	generic_where_list |
        range_predicate1_list | range_predicate2_list |
        range_predicate1_list and_or generic_where_list |
        range_predicate2_list and_or generic_where_list ;


generic_where_list:
        where_item |
        ( where_list and_or where_item ) ;

not:
	| | | NOT;

################################################################################
# The IS not NULL values in where_item are to hit the ref_or_null and          #
# the not_exists optimizations.  The LIKE '%a%' rule is to try to hit the      #
# rnd_pos optimization                                                         #
################################################################################
where_item:
        table1 .`pk` arithmetic_operator existing_table_item . int_field_name  |
        table1 .`pk` arithmetic_operator existing_table_item . int_field_name  |
        existing_table_item . int_field_name arithmetic_operator existing_table_item . int_field_name |
        existing_table_item . char_field_name arithmetic_operator existing_table_item . char_field_name |
        existing_table_item . int_field_name arithmetic_operator int_value  |
        existing_table_item . char_field_name arithmetic_operator char_value  |
        table1 .`pk` IS not NULL |
        table1 . _field IS not NULL |
        table1 . int_indexed arithmetic_operator int_value AND ( table1 . char_field_name LIKE '%a%' OR table1.char_field_name LIKE '%b%') ;

################################################################################
# The range_predicate_1* rules below are in place to ensure we hit the         #
# index_merge/sort_union optimization.                                         #
# NOTE: combinations of the predicate_1 and predicate_2 rules tend to hit the  #
# index_merge/intersect optimization                                           #
################################################################################

range_predicate1_list:
      range_predicate1_item | 
      ( range_predicate1_item OR range_predicate1_list ) ;

range_predicate1_item:
         table1 . int_indexed not BETWEEN _tinyint_unsigned[invariant] AND ( _tinyint_unsigned[invariant] + _tinyint_unsigned ) |
         table1 . `col_varchar_key` arithmetic_operator _char[invariant] |
         table1 . int_indexed not IN (number_list) |
         table1 . `col_varchar_key` not IN (char_list) |
         table1 . `pk` > _tinyint_unsigned[invariant] AND table1 . `pk` < ( _tinyint_unsigned[invariant] + _tinyint_unsigned ) |
         table1 . `col_int_key` > _tinyint_unsigned[invariant] AND table1 . `col_int_key` < ( _tinyint_unsigned[invariant] + _tinyint_unsigned ) ;

################################################################################
# The range_predicate_2* rules below are in place to ensure we hit the         #
# index_merge/union optimization.                                              #
# NOTE: combinations of the predicate_1 and predicate_2 rules tend to hit the  #
# index_merge/intersect optimization                                           #
################################################################################

range_predicate2_list:
      range_predicate2_item | 
      ( range_predicate2_item and_or range_predicate2_list ) ;

range_predicate2_item:
        table1 . `pk` = _tinyint_unsigned |
        table1 . `col_int_key` = _tinyint_unsigned |
        table1 . `col_varchar_key` = _char |
        table1 . int_indexed = _tinyint_unsigned |
        table1 . `col_varchar_key` = _char |
        table1 . int_indexed = existing_table_item . int_indexed |
        table1 . `col_varchar_key` = existing_table_item . `col_varchar_key` ;

################################################################################
# The number and char_list rules are for creating WHERE conditions that test   #
# 'field' IN (list_of_items)                                                   #
################################################################################
number_list:
        _tinyint_unsigned | number_list, _tinyint_unsigned ;

char_list: 
        _char | char_list, _char ;

################################################################################
# We ensure that a GROUP BY statement includes all nonaggregates.              #
# This helps to ensure the query is more useful in detecting real errors /     #
# that the query doesn't lend itself to variable result sets                   #
################################################################################
group_by_clause:
        |
	{ scalar(@nonaggregates) > 0 ? " GROUP BY ".join (', ' , @nonaggregates ) : "" }  ;

having_clause:
	| HAVING having_list;

having_list:
        having_item |
        having_item |
	(having_list and_or having_item)  ;

having_item:
	existing_select_item arithmetic_operator value ;

################################################################################
# We use the total_order_by rule when using the LIMIT operator to ensure that  #
# we have a consistent result set - server1 and server2 should not differ      #
################################################################################

order_by_clause:
	|
        ORDER BY table1 . _field_indexed desc , total_order_by  limit |
	ORDER BY order_by_list |
	ORDER BY  order_by_list, total_order_by limit ;

total_order_by:
	{ join(', ', map { "field".$_ } (1..$fields) ) };

order_by_list:
	order_by_item  |
	order_by_item  , order_by_list ;

order_by_item:
        table1 . _field_indexed , existing_table_item .`pk` desc  |
        table1 . _field_indexed desc |
	existing_select_item desc |
        CONCAT( existing_table_item . char_field_name, existing_table_item . char_field_name );
desc:
        ASC | | DESC ; 

################################################################################
# We mix digit and _digit here.  We want to alter the possible values of LIMIT #
# To ensure we hit varying EXPLAIN plans, but the OFFSET can be smaller        #
################################################################################

limit:
	| ;
#| LIMIT limit_size | LIMIT limit_size OFFSET _digit;

new_select_item:
	nonaggregate_select_item |
	nonaggregate_select_item |
#	aggregate_select_item |
        combo_select_item ;

################################################################################
# We have the perl code here to help us write more sensible queries            #
# It allows us to use field1...fieldn in the WHERE, ORDER BY, and GROUP BY     #
# clauses so that the queries will produce more stable and interesting results #
################################################################################

nonaggregate_select_item:
        table_one_two . _field_indexed AS { my $f = "field".++$fields ; push @nonaggregates , $f ; $f } |
        table_one_two . _field_indexed AS { my $f = "field".++$fields ; push @nonaggregates , $f ; $f } |
	table_one_two . _field AS { my $f = "field".++$fields ; push @nonaggregates , $f ; $f } ;

aggregate_select_item:
	aggregate table_one_two . _field ) AS { "field".++$fields };

################################################################################
# The combo_select_items are for 'spice' - we actually found                   #
################################################################################

combo_select_item:
    ( ( table_one_two . int_field_name ) math_operator ( table_one_two . int_field_name ) ) AS { my $f = "field".++$fields ; push @nonaggregates , $f ; $f } |
    CONCAT( table_one_two . char_field_name , table_one_two . char_field_name ) AS { my $f = "field".++$fields ; push @nonaggregates , $f ; $f } ;

table_one_two:
	table1 | table2 ;

aggregate:
	COUNT( distinct | SUM( distinct | MIN( distinct | MAX( distinct ;

################################################################################
# The following rules are for writing more sensible queries - that we don't    #
# reference tables / fields that aren't present in the query and that we keep  #
# track of what we have added.  You shouldn't need to touch these ever         #
################################################################################
new_table_item:
	_table AS { "table".++$tables };

current_table_item:
	{ "table".$tables };

previous_table_item:
	{ "table".($tables - 1) };

existing_table_item:
	{ "table".$prng->int(1,$tables) };

existing_select_item:
	{ "field".$prng->int(1,$fields) };
################################################################################
# end of utility rules                                                         #
################################################################################

arithmetic_operator:
	= | > | < | != | <> | <= | >= ;

################################################################################
# We are trying to skew the ON condition for JOINs to be largely based on      #
# equalities, but to still allow other arithmetic operators                    #
################################################################################
join_condition_operator:
    arithmetic_operator | = | = | = ;

################################################################################
# Used for creating combo_items - ie (field1 + field2) AS fieldX               #
# We ignore division to prevent division by zero errors                        #
################################################################################
math_operator:
    + | - | * ;

################################################################################
# We stack AND to provide more interesting options for the optimizer           #
# Alter these percentages at your own risk / look for coverage regressions     #
# with --debug if you play with these.  Those optimizations that require an    #
# OR-only list in the WHERE clause are specifically stacked in another rule    #
################################################################################
and_or:
   AND | AND | OR ;

	
value:
	_digit | _digit | _digit | _digit | _tinyint_unsigned|
        _char(2) | _char(2) | _char(2) | _char(2) | _char(2) ;

int_value:
	_digit | _digit | _digit | _digit | _tinyint_unsigned ;

char_value:
        _char(2);

date_value:
        _date;

_table:
     A | B | C | BB | CC | B | C | BB | CC | 
     C | C | C | C  | C  | C | C | C  | C  |
     CC | CC | CC | CC | CC | CC | CC | CC |
     D ;

################################################################################
# Add a possibility for 'view' to occur at the end of the previous '_table' rule
# to allow a chance to use views (when running the RQG with --views)
################################################################################

view:
    view_A | view_B | view_C | view_BB | view_CC ;

_field:
    int_field_name | char_field_name ;

_digit:
    1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | _tinyint_unsigned ;


int_field_name:
    `pk` | `col_int_key` | `col_int_nokey` ;

int_indexed:
    `pk` | `col_int_key` ;


char_field_name:
    `col_varchar_key` | `col_varchar_nokey` ;

################################################################################
# We define LIMIT_rows in this fashion as LIMIT values can differ depending on      #
# how large the LIMIT is - LIMIT 2 = LIMIT 9 != LIMIT 19                       #
################################################################################

limit_size:
    1 | 2 | 10 | 100 | 1000;
