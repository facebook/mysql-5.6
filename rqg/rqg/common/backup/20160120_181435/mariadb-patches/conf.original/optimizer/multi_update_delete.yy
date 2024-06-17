# Copyright (c) 2008, 2012, Oracle and/or its affiliates. All rights
# reserved.
#
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301
# USA

################################################################################
# multi_update_delete.yy: Random Query Generator grammar for testing the effect#
#			  of multi-table UPDATE and DELETE. This grammar is    # 
#			  recommended to run for engine=INNODB with a minumum  #
#			  query timeout of 120 seconds.	                       # 
################################################################################

query_init:
	SET AUTOCOMMIT=OFF ;

query:
        { @nonaggregates = () ; $tables = 0 ; $fields = 0 ; $subquery_idx=0 ; $child_subquery_idx=0 ; "" } main_query ;

main_query:
	main_update ; rollback | main_update ; rollback |
        main_update ; rollback | main_update ; rollback |
        main_delete ; rollback | main_delete ; rollback |
        main_delete ; rollback | main_delete ; rollback ;

main_update:
	explain_extended UPDATE priority_update ignore 
	outer_table_join_with_set_where_subquery_expression ;

main_delete:
        delete1 | delete1 | delete2 | delete2 ;

delete1:
        explain_extended DELETE priority_update quick ignore
        delete_tab
        FROM non_comma_join 
	WHERE subquery_expression_with_alias;

delete2:
        explain_extended DELETE priority_update quick ignore
        FROM delete_tab
        USING non_comma_join
        WHERE subquery_expression_with_alias ;

delete_tab:
        OUTR1.* | OUTR1.*, OUTR2.* ;

explain_extended:
        | | | | | | | | | explain_extended2 ;

explain_extended2: | | | | EXPLAIN | EXPLAIN EXTENDED ;

priority_update:
	| LOW_PRIORITY ;

ignore:
	| | | | | | IGNORE ;

quick:
        | | | | | | | | | | QUICK ;

outer_table_join_with_set_where_subquery_expression:
	non_comma_join set_update_with_alias WHERE subquery_expression_with_alias | 
	non_comma_join set_update_with_alias WHERE subquery_expression_with_alias | 
	non_comma_join set_update_with_alias WHERE subquery_expression_with_alias | 
	non_comma_join set_update_with_alias WHERE subquery_expression_with_alias | 
	non_comma_join set_update_with_alias WHERE subquery_expression_with_alias | 
	non_comma_join set_update_with_alias WHERE subquery_expression_with_alias | 
	comma_join set_update_without_alias WHERE subquery_expression_without_alias |
	comma_join set_update_without_alias WHERE subquery_expression_without_alias |
	comma_join set_update_without_alias WHERE subquery_expression_without_alias |
	comma_join set_update_without_alias WHERE subquery_expression_without_alias;

non_comma_join:
	o_tab AS OUTR1 join o_tab AS OUTR2 ON ( outer_join_condition ) |
   	o_tab AS OUTR1 join o_tab AS OUTR2 ON ( outer_join_condition ) join o_tab AS OUTR3 ON ( outer_join_condition2 ) ;
    	
comma_join:
	o_tab AS OUTR1, o_tab AS OUTR2 ;
#	o_tab AS OUTR1, o_tab AS OUTR2 WHERE ( outer_join_condition ) ;

not:
        | | | | NOT;

join:
	JOIN | RIGHT JOIN | LEFT JOIN | INNER JOIN | RIGHT OUTER JOIN | LEFT OUTER JOIN ;

outer_join_condition:
        OUTR1 . int_field_name = OUTR2 . int_field_name |
        OUTR1 . date_field_name = OUTR2 . date_field_name |
        OUTR1 . char_field_name = OUTR2 . char_field_name ;

outer_join_condition2:
	OUTR1 . int_field_name = OUTR3 . int_field_name |
	OUTR1 . date_field_name = OUTR3 . date_field_name |
	OUTR1 . char_field_name = OUTR3 . char_field_name ;

set_update_with_alias:
	SET OUTR1.set_field_name = value |
	SET OUTR1.set_field_name = value, OUTR2.set_field_name = value ; 

set_update_without_alias:
	SET OUTR1.char_field_name = value |
	SET OUTR1.char_field_name = value, OUTR2.char_field_name = value ;

subquery_expression_with_alias:
        OUTR1 . int_field_name arithmetic_operator ( SELECT select_option INNR1 . int_field_name AS y select_inner_body ) |
        OUTR1 . char_field_name arithmetic_operator ( SELECT select_option INNR1 . char_field_name AS y select_inner_body ) |
        OUTR1 . int_field_name membership_operator ( SELECT select_option INNR1 . int_field_name AS y select_inner_body ) |
        OUTR1 . char_field_name membership_operator ( SELECT select_option INNR1 . char_field_name AS y select_inner_body ) |
        OUTR1 . int_field_name not IN ( SELECT select_option INNR1 . int_field_name AS y select_inner_body ) |
        OUTR1 . int_field_name not IN ( SELECT select_option INNR1 . int_field_name AS y select_inner_body ) |
        OUTR1 . char_field_name not IN ( SELECT select_option INNR1 . char_field_name AS y select_inner_body ) |
        OUTR1 . char_field_name not IN ( SELECT select_option INNR1 . char_field_name AS y select_inner_body ) |
        ( OUTR1 . int_field_name , OUTR1 . int_field_name ) not IN ( SELECT select_option INNR1 . int_field_name AS x , INNR1 . int_field_name AS y select_inner_body ) |
        ( OUTR1 . int_field_name , OUTR1 . int_field_name ) not IN ( SELECT select_option INNR1 . int_field_name AS x , INNR1 . int_field_name AS y select_inner_body ) |
        ( OUTR1 . char_field_name , OUTR1 . char_field_name ) not IN ( SELECT select_option INNR1 . char_field_name AS x , INNR1 . char_field_name AS y select_inner_body ) |
        ( OUTR1 . char_field_name , OUTR1 . char_field_name ) not IN ( SELECT select_option INNR1 . char_field_name AS x , INNR1 . char_field_name AS y select_inner_body ) |
	( _digit, _digit ) not IN ( SELECT select_option INNR1 . int_field_name AS x , INNR1 . int_field_name AS y select_inner_body ) |
	( _char, _char ) not IN ( SELECT select_option INNR1 . char_field_name AS x , INNR1 . char_field_name AS y select_inner_body ) |
	(_digit, _digit ) not IN ( SELECT select_option INNR1 . int_field_name AS x , INNR1 . int_field_name AS y select_inner_body ) |
	( _char, _char ) not IN ( SELECT select_option INNR1 . char_field_name AS x , INNR1 . char_field_name AS y select_inner_body ) |
        OUTR1 . int_field_name membership_operator ( int_single_union_subquery ) |
        OUTR1 . char_field_name membership_operator (  char_single_union_subquery ) ;

subquery_expression_without_alias:
	OUTR1.int_field_name not IN ( SELECT select_option INNR1 . int_field_name AS y select_inner_body_without_alias ) |
	OUTR1.char_field_name not IN ( SELECT select_option INNR1 . char_field_name AS y select_inner_body_without_alias) |
	OUTR1.int_field_name arithmetic_operator ( SELECT select_option INNR1 . int_field_name AS y select_inner_body ) |
	OUTR1.char_field_name arithmetic_operator ( SELECT select_option INNR1 . char_field_name AS y select_inner_body ) |
 	OUTR1.int_field_name membership_operator ( SELECT select_option INNR1 . int_field_name AS y select_inner_body ) |
	OUTR1.char_field_name membership_operator ( SELECT select_option INNR1 . char_field_name AS y select_inner_body ) |
	( OUTR1.int_field_name, OUTR1.int_field_name ) not IN ( SELECT select_option INNR1 . int_field_name AS x , INNR1 . int_field_name AS y select_inner_body ) |
	( OUTR1.char_field_name, OUTR1.char_field_name ) not IN ( SELECT select_option INNR1 . char_field_name AS x , INNR1 . char_field_name AS y select_inner_body ) |
	( _digit, _digit ) not IN ( SELECT select_option INNR1 . int_field_name AS x , INNR1 . int_field_name AS y select_inner_body ) |
        ( _char, _char ) not IN ( SELECT select_option INNR1 . char_field_name AS x , INNR1 . char_field_name AS y select_inner_body ) |
	OUTR1.int_field_name membership_operator ( int_single_union_subquery ) |
	OUTR1.char_field_name membership_operator (  char_single_union_subquery ) ;

select_inner_body:
        FROM inner_from
        WHERE inner_condition_top
        inner_order_by ;

select_inner_body_without_alias:
        FROM inner_from
        WHERE inner_condition_top1
        inner_order_by ;

inner_from:
        i_tab AS INNR1 |
        i_tab AS INNR2 join i_tab AS INNR1 ON ( inner_join_condition );

inner_order_by:
        | ORDER BY INNR1 . field_name ;

inner_join_condition:
        INNR2 . int_field_name arithmetic_operator INNR1 . int_field_name |
        INNR2 . char_field_name arithmetic_operator INNR1 . char_field_name |
        INNR2 . date_field_name arithmetic_operator INNR1 . date_field_name ;

inner_condition_top:
        INNR1 . expression |
        OUTR1 . expression |
        inner_condition_bottom logical_operator inner_condition_bottom |
        inner_condition_bottom logical_operator outer_condition_bottom ;

inner_condition_top1:
        INNR1 . expression |
        inner_condition_bottom logical_operator inner_condition_bottom ;

expression:
        field_name null_operator |
        int_field_name int_expression |
        date_field_name date_expression |
        char_field_name char_expression ;

int_expression:
        arithmetic_operator digit ;

date_expression:
        arithmetic_operator date | BETWEEN date AND date ;

char_expression:
        arithmetic_operator _varchar(1) ;

inner_condition_bottom:
        INNR1 . expression |
        INNR1 . int_field_name arithmetic_operator INNR1 . int_field_name |
        INNR1 . date_field_name arithmetic_operator INNR1 . date_field_name |
        INNR1 . char_field_name arithmetic_operator INNR1 . char_field_name ;

outer_condition_bottom:
        OUTR2 . expression ;

int_single_union_subquery:
	SELECT _digit  UNION all_distinct  SELECT _digit ;

char_single_union_subquery:
	SELECT _char  UNION all_distinct  SELECT _char ;

null_operator: IS NULL | IS NOT NULL ;

logical_operator:
        AND | OR | OR NOT | XOR | AND NOT ;

arithmetic_operator:
        = | > | < | <> | >= | <= ;

membership_operator:
	arithmetic_operator all_any |
	not IN |
	not IN |
	not IN |
	not IN ;

all_any:
	ALL | ANY | SOME ;

all_distinct:
	| | | |
	| | ALL | DISTINCT ;

field_name:
        int_field_name | char_field_name | date_field_name;

int_field_name:
        `pk` | `col_int_key` | `col_int_nokey` ;

date_field_name:
        `col_date_key` | `col_date_nokey` | `col_datetime_key` | `col_datetime_nokey` ;

char_field_name:
        `col_varchar_key` | `col_varchar_nokey` ;

set_field_name:
	`col_int_nokey` | `col_int_key` | `col_varchar_nokey` | `col_varchar_key` ;

o_tab:
        A | B | C | D | E ;

i_tab:
        AA | BB | CC | DD ;

rollback:
	ROLLBACK ;

value:
        _data | _data | _varchar(256) | _varchar(1024) | _char(256) | _char(1024) | _english | _english | NULL | NULL | 0 | 1 | -1 ;

select_option:
        | DISTINCT ;
