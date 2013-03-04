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
# optimizer_subquery.yy:  Random Query Generator grammar for testing subquery  #
#                    optimizations.  This grammar *should* hit the             # 
#                    optimizations listed here:                                #
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
	{ @nonaggregates = () ; $tables = 0 ; $fields = 0 ; $subquery_idx=0 ; $child_subquery_idx=0 ; "" } main_select ;

main_select:
        simple_select | simple_select | simple_select | simple_select |
        mixed_select |  mixed_select |  mixed_select |  mixed_select  | 
        aggregate_select ;

mixed_select:
	explain_extended SELECT distinct straight_join select_option select_list
	FROM join_list
	where_clause
	group_by_clause
        having_clause
	order_by_clause ;

simple_select:
        explain_extended SELECT distinct straight_join select_option simple_select_list
        FROM join_list
        where_clause
        optional_group_by
        having_clause
        order_by_clause ;
 
aggregate_select:
        explain_extended SELECT distinct straight_join select_option aggregate_select_list
        FROM join_list
        where_clause
        optional_group_by
        having_clause
        order_by_clause ;

explain_extended:
    | | | | | | | | | explain_extended2 ;

explain_extended2: | | | | EXPLAIN | EXPLAIN EXTENDED ; 
       
distinct: DISTINCT | | | | | | | | | ;

select_option:  | | | | | | | | | | | SQL_SMALL_RESULT ;

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

join_list:
################################################################################
# this limits us to 2 and 3 table joins / can use it if we hit                 #
# too many mega-join conditions which take too long to run                     #
################################################################################
	( new_table_item join_type new_table_item ON (join_condition_item ) ) |
        ( new_table_item join_type ( ( new_table_item join_type new_table_item ON (join_condition_item ) ) ) ON (join_condition_item ) ) ;

join_list_disabled:
################################################################################
# preventing deep join nesting for run time / table access methods are more    #
# important here - join.yy can provide deeper join coverage                    #
# Enabling this / swapping out with join_list above can produce some           #
# time-consuming queries.                                                      #
################################################################################

        new_table_item |
        ( new_table_item join_type join_list ON (join_condition_item ) ) ;

join_type:
	INNER JOIN | left_right outer JOIN | STRAIGHT_JOIN ;  

join_condition_item:
    current_table_item . int_indexed = previous_table_item . int_field_name on_subquery |
    current_table_item . int_field_name = previous_table_item . int_indexed on_subquery |
    current_table_item . char_indexed = previous_table_item . char_field_name on_subquery |
    current_table_item . char_field_name = previous_table_item . char_indexed on_subquery ;

on_subquery:
    |||||||||||||||||||| { $subquery_idx += 1 ; $subquery_tables=0 ; ""} and_or general_subquery ;


left_right:
	LEFT | RIGHT ;

outer:
	| OUTER ;

where_clause:
         WHERE ( where_subquery ) and_or where_list ;


where_list:
	generic_where_list |
        range_predicate1_list | range_predicate2_list |
        range_predicate1_list and_or generic_where_list |
        range_predicate2_list and_or generic_where_list ; 


generic_where_list:
        where_item |
        ( where_item and_or where_item ) ;

not:
	| | | NOT;

where_item:
        where_subquery  |  
        table1 . int_field_name arithmetic_operator existing_table_item . int_field_name  |
	existing_table_item . char_field_name arithmetic_operator _char  |
        existing_table_item . char_field_name arithmetic_operator existing_table_item . char_field_name |
        table1 . _field IS not NULL |
        table1 . int_field_name arithmetic_operator existing_table_item . int_field_name  |
	existing_table_item . char_field_name arithmetic_operator _char  |
        existing_table_item . char_field_name arithmetic_operator existing_table_item . char_field_name |
        table1 . _field IS not NULL ;

################################################################################
# subquery rules
################################################################################

where_subquery:
    { $subquery_idx += 1 ; $subquery_tables=0 ; ""} subquery_type ;

subquery_type:
    general_subquery | special_subquery ;

general_subquery:
    existing_table_item . int_field_name arithmetic_operator  int_single_value_subquery  |
    existing_table_item . char_field_name arithmetic_operator char_single_value_subquery |
    existing_table_item . int_field_name membership_operator  int_single_member_subquery  |
    ( existing_table_item . int_field_name , existing_table_item . int_field_name ) not IN int_double_member_subquery |
    existing_table_item . char_field_name membership_operator  char_single_member_subquery  |
    ( existing_table_item . char_field_name , existing_table_item . char_field_name ) not IN char_double_member_subquery |
    ( _digit, _digit ) not IN int_double_member_subquery |
    ( _char, _char ) not IN char_double_member_subquery |
    existing_table_item . int_field_name membership_operator int_single_union_subquery |
    existing_table_item . char_field_name membership_operator char_single_union_subquery ;

general_subquery_union_test_disabled:
    existing_table_item . char_field_name arithmetic_operator all_any char_single_union_subquery_disabled |
    existing_table_item . int_field_name arithmetic_operator all_any int_single_union_subquery_disabled ;

special_subquery:
    not EXISTS ( int_single_member_subquery ) |
    not EXISTS ( char_single_member_subquery ) |
    not EXISTS int_correlated_subquery |
    not EXISTS char_correlated_subquery  | 
    existing_table_item . int_field_name membership_operator  int_correlated_subquery  |
    existing_table_item . char_field_name membership_operator char_correlated_subquery  |
    int_single_value_subquery IS not NULL |
    char_single_value_subquery IS not NULL ;

int_single_value_subquery:
    ( SELECT distinct select_option aggregate subquery_table_one_two . int_field_name ) AS { "SUBQUERY".$subquery_idx."_field1" } 
      subquery_body ) |
    ( SELECT distinct select_option aggregate subquery_table_one_two . int_field_name ) AS { "SUBQUERY".$subquery_idx."_field1" } 
      subquery_body ) ; 

char_single_value_subquery:
    ( SELECT distinct select_option aggregate subquery_table_one_two . char_field_name ) AS { "SUBQUERY".$subquery_idx."_field1" } 
      subquery_body ) |
    ( SELECT distinct select_option aggregate subquery_table_one_two . char_field_name ) AS { "SUBQUERY".$subquery_idx."_field1" } 
      subquery_body ) ; 
   
int_single_member_subquery:
    ( SELECT distinct select_option subquery_table_one_two . int_field_name AS { "SUBQUERY".$subquery_idx."_field1" }
      subquery_body 
      single_subquery_group_by
      subquery_having ) ; 

int_single_union_subquery:
    (  SELECT _digit  UNION all_distinct  SELECT _digit  )  ;

int_single_union_subquery_disabled:
    int_single_member_subquery   UNION all_distinct  int_single_member_subquery ;

int_double_member_subquery:
    ( SELECT distinct select_option subquery_table_one_two . int_field_name AS { "SUBQUERY".$subquery_idx."_field1" } , 
      subquery_table_one_two . int_field_name AS { SUBQUERY.$subquery_idx."_field2" }
      subquery_body 
      double_subquery_group_by
      subquery_having ) |
    ( SELECT distinct select_option subquery_table_one_two . int_field_name AS { "SUBQUERY".$subquery_idx."_field1" } , 
      aggregate subquery_table_one_two . int_field_name ) AS { SUBQUERY.$subquery_idx."_field2" }
      subquery_body 
      single_subquery_group_by
      subquery_having ) |
    (  SELECT _digit , _digit  UNION all_distinct  SELECT _digit, _digit  ) ;

char_single_member_subquery:
    ( SELECT distinct select_option subquery_table_one_two . char_field_name AS { "SUBQUERY".$subquery_idx."_field1" }
     subquery_body
     single_subquery_group_by
     subquery_having) ;

char_single_union_subquery:
    (  SELECT _char  UNION all_distinct  SELECT _char  )  ;

char_single_union_subquery_disabled:
    char_single_member_subquery   UNION all_distinct char_single_member_subquery  ;

char_double_member_subquery:
   ( SELECT distinct select_option subquery_table_one_two . char_field_name AS { "SUBQUERY".$subquery_idx."_field1" } ,
     subquery_table_one_two . char_field_name AS { SUBQUERY.$subquery_idx."_field2" }
     subquery_body
     double_subquery_group_by
     subquery_having ) |
   ( SELECT distinct select_option subquery_table_one_two . char_field_name AS { "SUBQUERY".$subquery_idx."_field1" } ,
     aggregate subquery_table_one_two . char_field_name ) AS { SUBQUERY.$subquery_idx."_field2" }
     subquery_body
     single_subquery_group_by
     subquery_having ) |
   (  SELECT _char , _char  UNION all_distinct  SELECT _char , _char  ) ;

int_correlated_subquery:
    ( SELECT distinct select_option subquery_table_one_two . int_field_name AS { "SUBQUERY".$subquery_idx."_field1" }
      FROM subquery_join_list 
      correlated_subquery_where_clause ) ;

char_correlated_subquery:
    ( SELECT distinct select_option subquery_table_one_two . char_field_name AS { "SUBQUERY".$subquery_idx."_field1" }
      FROM subquery_join_list 
      correlated_subquery_where_clause ) ;

int_scalar_correlated_subquery:
     ( SELECT distinct select_option aggregate subquery_table_one_two . int_field_name ) AS { "SUBQUERY".$subquery_idx."_field1" }
      FROM subquery_join_list 
      correlated_subquery_where_clause ) ;

subquery_body:
      FROM subquery_join_list
      subquery_where_clause ;

subquery_where_clause:
    | | WHERE subquery_where_list ;

correlated_subquery_where_clause:
    WHERE correlated_subquery_where_list ;

correlated_subquery_where_list:
    correlated_subquery_where_item |
    correlated_subquery_where_item and_or correlated_subquery_where_item |
    correlated_subquery_where_item and_or subquery_where_item ;

correlated_subquery_where_item:
    existing_subquery_table_item . int_field_name arithmetic_operator existing_table_item . int_field_name |
    existing_subquery_table_item . char_field_name arithmetic_operator existing_table_item . char_field_name ;

subquery_where_list:
    subquery_where_item | subquery_where_item | subquery_where_item |
    ( subquery_where_item and_or subquery_where_item ) ;

subquery_where_item:
   existing_subquery_table_item . int_field_name arithmetic_operator _digit |
   existing_subquery_table_item . char_field_name arithmetic_operator _char |
   existing_subquery_table_item . int_field_name arithmetic_operator existing_subquery_table_item . int_field_name |
   existing_subquery_table_item . char_field_name arithmetic_operator existing_subquery_table_item . char_field_name |
   child_subquery ;

subquery_join_list:
    subquery_new_table_item  |  subquery_new_table_item  |
   ( subquery_new_table_item join_type subquery_new_table_item ON (subquery_join_condition_item ) ) |
   ( subquery_new_table_item join_type subquery_new_table_item ON (subquery_join_condition_item ) ) |
   ( subquery_new_table_item join_type ( subquery_new_table_item join_type subquery_new_table_item ON (subquery_join_condition_item )  ) ON (subquery_join_condition_item ) ) ;

subquery_join_condition_item:
    subquery_current_table_item . int_field_name = subquery_previous_table_item . int_indexed subquery_on_subquery |
    subquery_current_table_item . int_indexed = subquery_previous_table_item . int_field_name subquery_on_subquery |
    subquery_current_table_item . char_indexed = subquery_previous_table_item . char_field_name subquery_on_subquery |
    subquery_current_table_item . char_field_name = subquery_previous_table_item . char_indexed subquery_on_subquery ;

subquery_on_subquery:
    |||||||||||||||||||| { $child_subquery_idx += 1 ; $child_subquery_tables=0 ; ""} and_or general_child_subquery ;

single_subquery_group_by:
    | | | | | | | | | GROUP BY { SUBQUERY.$subquery_idx."_field1" } ;


double_subquery_group_by:
    | | | | | | | | | GROUP BY { SUBQUERY.$subquery_idx."_field1" } ,  { SUBQUERY.$subquery_idx."_field2" } ;

subquery_having:
    | | | | | | | | | | HAVING subquery_having_list ;

subquery_having_list:
        subquery_having_item |
        subquery_having_item |
	(subquery_having_list and_or subquery_having_item)  ;

subquery_having_item:
	existing_subquery_table_item . int_field_name arithmetic_operator _digit |
        existing_subquery_table_item . int_field_name arithmetic_operator _char ;


################################################################################
# Child subquery rules
################################################################################

child_subquery:
    { $child_subquery_idx += 1 ; $child_subquery_tables=0 ; ""} child_subquery_type ;

child_subquery_type:
    general_child_subquery | special_child_subquery ;

general_child_subquery:
    existing_subquery_table_item . int_field_name arithmetic_operator  int_single_value_child_subquery  |
    existing_subquery_table_item . char_field_name arithmetic_operator char_single_value_child_subquery |
    existing_subquery_table_item . int_field_name membership_operator  int_single_member_child_subquery  |
    ( existing_subquery_table_item . int_field_name , existing_subquery_table_item . int_field_name ) not IN int_double_member_child_subquery |
    existing_subquery_table_item . char_field_name membership_operator  char_single_member_child_subquery  |
    ( existing_subquery_table_item . char_field_name , existing_subquery_table_item . char_field_name ) not IN char_double_member_child_subquery |
    ( _digit, _digit ) not IN int_double_member_child_subquery |
    ( _char, _char ) not IN char_double_member_child_subquery |
    existing_subquery_table_item . int_field_name membership_operator int_single_union_child_subquery |
    existing_subquery_table_item . char_field_name membership_operator char_single_union_child_subquery ;

special_child_subquery:
    not EXISTS ( int_single_member_child_subquery ) |
    not EXISTS ( char_single_member_child_subquery ) |
    not EXISTS int_correlated_child_subquery |
    not EXISTS char_correlated_child_subquery |
    existing_subquery_table_item . int_field_name membership_operator  int_correlated_child_subquery  |
    existing_subquery_table_item . char_field_name membership_operator char_correlated_child_subquery ;


int_single_value_child_subquery:
    ( SELECT distinct select_option aggregate child_subquery_table_one_two . int_field_name ) AS { "CHILD_SUBQUERY".$child_subquery_idx."_field1" } 
      child_subquery_body ) ;

char_single_value_child_subquery:
    ( SELECT distinct select_option aggregate child_subquery_table_one_two . char_field_name ) AS { "CHILD_SUBQUERY".$child_subquery_idx."_field1" } 
      child_subquery_body ) ;
   
int_single_member_child_subquery:
    ( SELECT distinct select_option child_subquery_table_one_two . int_field_name AS { "CHILD_SUBQUERY".$child_subquery_idx."_field1" }
      child_subquery_body 
      single_child_subquery_group_by
      child_subquery_having ) ;

int_single_union_child_subquery:
    (  SELECT _digit  UNION all_distinct  SELECT _digit  )  ;

int_double_member_child_subquery:
    ( SELECT distinct select_option child_subquery_table_one_two . int_field_name AS { "CHILD_SUBQUERY".$child_subquery_idx."_field1" } , 
      child_subquery_table_one_two . int_field_name AS { child_subquery.$child_subquery_idx."_field2" }
      child_subquery_body 
      double_child_subquery_group_by
      child_subquery_having ) |
    ( SELECT distinct select_option child_subquery_table_one_two . int_field_name AS { "CHILD_SUBQUERY".$child_subquery_idx."_field1" } , 
      aggregate child_subquery_table_one_two . int_field_name ) AS { child_subquery.$child_subquery_idx."_field2" }
      child_subquery_body 
      single_child_subquery_group_by
      child_subquery_having );

char_single_member_child_subquery:
    ( SELECT distinct select_option child_subquery_table_one_two . char_field_name AS { "CHILD_SUBQUERY".$child_subquery_idx."_field1" }
     child_subquery_body
     single_child_subquery_group_by
     child_subquery_having) ;

char_single_union_child_subquery:
    (  SELECT _digit  UNION all_distinct  SELECT _digit  )  ;

char_double_member_child_subquery:
   ( SELECT distinct select_option child_subquery_table_one_two . char_field_name AS { "CHILD_SUBQUERY".$child_subquery_idx."_field1" } ,
     child_subquery_table_one_two . char_field_name AS { "CHILD_SUBQUERY".$child_subquery_idx."_field2" }
     child_subquery_body
     double_child_subquery_group_by
     child_subquery_having ) |
   ( SELECT distinct select_option child_subquery_table_one_two . char_field_name AS { "CHILD_SUBQUERY".$child_subquery_idx."_field1" } ,
     aggregate child_subquery_table_one_two . char_field_name ) AS { "CHILD_SUBQUERY".$child_subquery_idx."_field2" }
     child_subquery_body
     single_child_subquery_group_by
     child_subquery_having );

int_correlated_child_subquery:
    ( SELECT distinct select_option child_subquery_table_one_two . int_field_name AS { "CHILD_SUBQUERY".$subquery_idx."_field1" }
      FROM child_subquery_join_list 
      correlated_child_subquery_where_clause ) ;

char_correlated_child_subquery:
    ( SELECT distinct select_option child_subquery_table_one_two . char_field_name AS { "CHILD_SUBQUERY".$subquery_idx."_field1" }
      FROM child_subquery_join_list 
      correlated_child_subquery_where_clause ) ;

child_subquery_body:
      FROM child_subquery_join_list
      child_subquery_where_clause ;

child_subquery_where_clause:
    | WHERE child_subquery_where_list ;

correlated_child_subquery_where_clause:
    WHERE correlated_child_subquery_where_list ;

correlated_child_subquery_where_list:
    correlated_child_subquery_where_item | correlated_child_subquery_where_item | correlated_child_subquery_where_item |
    correlated_child_subquery_where_item and_or correlated_child_subquery_where_item |
    correlated_child_subquery_where_item and_or child_subquery_where_item ;

correlated_child_subquery_where_item:
    existing_child_subquery_table_item . int_field_name arithmetic_operator existing_subquery_table_item . int_field_name |
    existing_child_subquery_table_item . char_field_name arithmetic_operator existing_subquery_table_item . char_field_name ;

child_subquery_where_list:
    child_subquery_where_item | child_subquery_where_item | child_subquery_where_item |
    ( child_subquery_where_item and_or child_subquery_where_item ) ;

child_subquery_where_item:
   existing_child_subquery_table_item . int_field_name arithmetic_operator _digit |
   existing_child_subquery_table_item . char_field_name arithmetic_operator _char |
   existing_child_subquery_table_item . int_field_name arithmetic_operator existing_child_subquery_table_item . int_field_name |
   existing_child_subquery_table_item . char_field_name arithmetic_operator existing_child_subquery_table_item . char_field_name |
   child_child_subquery ;

child_subquery_join_list:
    child_subquery_new_table_item  |  child_subquery_new_table_item  |
   ( child_subquery_new_table_item join_type child_subquery_new_table_item ON (child_subquery_join_condition_item ) ) |
   ( child_subquery_new_table_item join_type child_subquery_new_table_item ON (child_subquery_join_condition_item ) ) |
   ( child_subquery_new_table_item join_type ( ( child_subquery_new_table_item join_type child_subquery_new_table_item ON (child_subquery_join_condition_item ) ) ) ON (child_subquery_join_condition_item ) ) ;

child_subquery_join_condition_item:
    child_subquery_current_table_item . int_field_name = child_subquery_previous_table_item . int_indexed |
    child_subquery_current_table_item . int_indexed = child_subquery_previous_table_item . int_field_name |
    child_subquery_current_table_item . char_indexed = child_subquery_previous_table_item . char_field_name |
    child_subquery_current_table_item . char_field_name = child_subquery_previous_table_item . char_indexed ;

single_child_subquery_group_by:
    | | | | | | | | | GROUP BY { child_subquery.$child_subquery_idx."_field1" } ;


double_child_subquery_group_by:
    | | | | | | | | | GROUP BY { child_subquery.$child_subquery_idx."_field1" } ,  { child_subquery.$child_subquery_idx."_field2" } ;

child_subquery_having:
    | | | | | | | | | | HAVING child_subquery_having_list ;

child_subquery_having_list:
        child_subquery_having_item |
        child_subquery_having_item |
	(child_subquery_having_list and_or child_subquery_having_item)  ;

child_subquery_having_item:
	existing_child_subquery_table_item . int_field_name arithmetic_operator _digit |
        existing_child_subquery_table_item . int_field_name arithmetic_operator _char ;


################################################################################
# The range_predicate_1* rules below are in place to ensure we hit the         #
# index_merge/sort_union optimization.                                         #
# NOTE: combinations of the predicate_1 and predicate_2 rules tend to hit the  #
# index_merge/intersect optimization                                           #
################################################################################

range_predicate1_list:
      range_predicate1_item | 
      ( range_predicate1_item OR range_predicate1_item ) ;

range_predicate1_item:
         table1 . int_indexed not BETWEEN _tinyint_unsigned[invariant] AND ( _tinyint_unsigned[invariant] + _tinyint_unsigned ) |
         table1 . char_indexed arithmetic_operator _char[invariant]  |
         table1 . int_indexed not IN (number_list) |
         table1 . char_indexed not IN (char_list) |
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
      ( range_predicate2_item and_or range_predicate2_item ) ;

range_predicate2_item:
        table1 . `pk` = _tinyint_unsigned |
        table1 . `col_int_key` = _tinyint_unsigned |
        table1 . char_indexed = _char |
        table1 . int_indexed = _tinyint_unsigned |
        table1 . char_indexed = _char |
        table1 . int_indexed = existing_table_item . int_indexed |
        table1 . char_indexed = existing_table_item . char_indexed ;

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
	{ scalar(@nonaggregates) > 0 ? " GROUP BY ".join (', ' , @nonaggregates ) : "" }  ;

optional_group_by:
        | | group_by_clause ;

having_clause:
	| HAVING having_list;

having_list:
        having_item |
        having_item |
	(having_list and_or having_item)  ;

having_item:
	existing_select_item arithmetic_operator value |
        existing_select_item arithmetic_operator value |
        existing_select_item arithmetic_operator value |
        existing_select_item arithmetic_operator value |
        existing_select_item arithmetic_operator value |
        existing_select_item arithmetic_operator value |
        { $subquery_idx += 1 ; $subquery_tables=0 ; ""} general_subquery;

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
        CONCAT ( existing_table_item . char_field_name, existing_table_item . char_field_name );
desc:
        ASC | | DESC ; 


limit:
	| | LIMIT limit_size | LIMIT limit_size OFFSET _digit;

new_select_item:
	nonaggregate_select_item |
	nonaggregate_select_item |
	aggregate_select_item |
        combo_select_item |
        nonaggregate_select_item |
	nonaggregate_select_item |
	aggregate_select_item |
        select_subquery;

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

select_subquery:
         { $subquery_idx += 1 ; $subquery_tables=0 ; ""} select_subquery_body;

select_subquery_body:
         int_single_value_subquery AS { my $f = "field".++$fields ; push @nonaggregates , $f ; $f } |
         char_single_value_subquery AS { my $f = "field".++$fields ; push @nonaggregates , $f ; $f } |
         int_scalar_correlated_subquery AS  { my $f = "field".++$fields ; push @nonaggregates , $f ; $f } ;

select_subquery_body_disabled:
         (  SELECT _digit  UNION all_distinct  ( SELECT _digit ) LIMIT 1 )  AS  { my $f = "field".++$fields ; push @nonaggregates , $f ; $f } |
         (  SELECT _char  UNION all_distinct ( SELECT _char ) LIMIT 1 )  AS  { my $f = "field".++$fields ; push @nonaggregates , $f ; $f } ;

################################################################################
# The combo_select_items are for 'spice' 
################################################################################

combo_select_item:
    ( ( table_one_two . int_field_name ) math_operator ( table_one_two . int_field_name ) ) AS { my $f = "field".++$fields ; push @nonaggregates , $f ; $f } |
    CONCAT ( table_one_two . char_field_name , table_one_two . char_field_name ) AS { my $f = "field".++$fields ; push @nonaggregates , $f ; $f } ;

table_one_two:
	table1 | table1 | table2 ;

subquery_table_one_two:
        { "SUBQUERY".$subquery_idx."_t1" ;  } | { "SUBQUERY".$subquery_idx."_t1" ;  } |
        { "SUBQUERY".$subquery_idx."_t1" ;  } | { "SUBQUERY".$subquery_idx."_t2" ;  } ;      

child_subquery_table_one_two:
        { "CHILD_SUBQUERY".$child_subquery_idx."_t1" ;  } | { "CHILD_SUBQUERY".$child_subquery_idx."_t1" ;  } |
        { "CHILD_SUBQUERY".$child_subquery_idx."_t1" ;  } | { "CHILD_SUBQUERY".$child_subquery_idx."_t2" ;  } ;

aggregate:
	COUNT( distinct | SUM( distinct | MIN( distinct | MAX( distinct ;

################################################################################
# The following rules are for writing more sensible queries - that we don't    #
# reference tables / fields that aren't present in the query and that we keep  #
# track of what we have added.  You shouldn't need to touch these ever         #
################################################################################
new_table_item:
	_table AS { "table".++$tables } | _table AS { "table".++$tables } | _table AS { "table".++$tables } |
        ( from_subquery ) AS { "table".++$tables } ;

from_subquery:
       { $subquery_idx += 1 ; $subquery_tables=0 ; ""}  SELECT distinct select_option subquery_table_one_two . * subquery_body  ;

subquery_new_table_item:
        _table AS { "SUBQUERY".$subquery_idx."_t".++$subquery_tables } ;

child_subquery_new_table_item:
        _table AS { "CHILD_SUBQUERY".$child_subquery_idx."_t".++$child_subquery_tables } ;      

current_table_item:
	{ "table".$tables };

subquery_current_table_item:
        { "SUBQUERY".$subquery_idx."_t".$subquery_tables } ;

child_subquery_current_table_item:
        { "CHILD_SUBQUERY".$child_subquery_idx."_t".$child_subquery_tables } ;

previous_table_item:
	{ "table".($tables - 1) };

subquery_previous_table_item:
        { "SUBQUERY".$subquery_idx."_t".($subquery_tables-1) } ;

child_subquery_previous_table_item:
        { "CHILD_SUBQUERY".$child_subquery_idx."_t".($child_subquery_tables-1) } ;

existing_table_item:
	{ "table".$prng->int(1,$tables) };

existing_subquery_table_item:
        { "SUBQUERY".$subquery_idx."_t".$prng->int(1,$subquery_tables) } ;

existing_child_subquery_table_item:
        { "CHILD_SUBQUERY".$child_subquery_idx."_t".$prng->int(1,$child_subquery_tables) } ;

existing_select_item:
	{ "field".$prng->int(1,$fields) };

################################################################################
# end of utility rules                                                         #
################################################################################

arithmetic_operator:
	= | > | < | != | <> | <= | >= ;


membership_operator:
    arithmetic_operator all_any |
    not IN ;

all_any:
    ALL | ANY | SOME ;

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

all_distinct:
   | | | | |
   | | | ALL | DISTINCT ;

	
value:
	_digit | _digit | _digit | _digit | _tinyint_unsigned|
        _char(2) | _char(2) | _char(2) | _char(2) | _char(2) ;

_table:
  AA | AA | AA | BB | BB | BB |
  CC | CC | DD | small_table ;

small_table:
  A | B | C | C | C | D | D | D ;

_field:
    int_field_name | char_field_name ;

_digit:
    1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | _tinyint_unsigned ;

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

################################################################################
# We define LIMIT_rows in this fashion as LIMIT values can differ depending on      #
# how large the LIMIT is - LIMIT 2 = LIMIT 9 != LIMIT 19                       #
################################################################################

limit_size:
    1 | 2 | 10 | 100 | 1000;
