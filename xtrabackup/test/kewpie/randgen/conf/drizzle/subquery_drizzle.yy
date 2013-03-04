# Copyright (C) 2009 Sun Microsystems, Inc. All rights reserved.
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

query: select;

query_disabled:
	EXPLAIN select;

select: 
	SELECT select_option outer_select_item
	FROM outer_from
	WHERE subquery_expression logical_operator outer_condition_top
	outer_group_by outer_having outer_order_by limit;

outer_select_item:
	OUTR . field_name AS X |
	aggregate_function OUTR . field_name ) AS X;

aggregate_function:
	AVG( |
	COUNT(DISTINCT | COUNT( |
	MIN( | MIN(DISTINCT |
	MAX( | MAX(DISTINCT |
	STD( | STDDEV_POP( | STDDEV_SAMP( |
	SUM( | SUM(DISTINCT |
	VAR_POP( | VAR_SAMP( | VARIANCE( ;

aggregate_function_disabled_false_positives:
	GROUP_CONCAT( | ;

aggregate_function_disabled:
 AVG(DISTINCT ;

outer_from:
	outer_table_name AS OUTR ;

outer_from_disabled_unnecessary_complication:
	outer_table_name AS OUTR2 LEFT JOIN outer_table_name AS OUTR ON ( outer_join_condition );

outer_join_condition:
	OUTR2 . int_field_name arithmetic_operator OUTR . int_field_name |
	OUTR2 . char_field_name arithmetic_operator OUTR . char_field_name ;

outer_order_by:
	ORDER BY OUTR . field_name , OUTR . `pk` ;

outer_group_by:
	| GROUP BY OUTR . field_name ;

outer_group_by_disabled_ha_reset:
	GROUP BY OUTR . field_name WITH ROLLUP;

outer_having:
	| HAVING X arithmetic_operator value ;

limit:
	| LIMIT digit ;

select_inner_body:
	FROM inner_from
	WHERE inner_condition_top
	inner_order_by;

select_inner:
	SELECT select_option inner_select_item
	select_inner_body;

select_inner_one_row:
	SELECT select_option inner_select_item
	select_inner_body LIMIT 1;

select_inner_two_cols:
	SELECT select_option inner_select_item , INNR . field_name AS Z 
	select_inner_body;

inner_order_by:
	| ORDER BY INNR . field_name ;

inner_group_by:
	| | | ;
inner_group_by_disabled:
	| GROUP BY INNR . field_name |
	GROUP BY INNR . field_name WITH ROLLUP;

inner_having:
	| | | HAVING X arithmetic_operator value;

inner_select_item:
	INNR . field_name AS Y ;

inner_select_item_disabled_causes_semijoin_not_to_kick_in;
	aggregate_function INNR . field_name ) AS Y ;

inner_from:
	inner_table_name AS INNR ;

inner_from_disabled_unnecessary_complication:
	inner_table_name AS INNR2 LEFT JOIN inner_table_name AS INNR ON ( inner_join_condition );

inner_join_condition:
	INNR2 . int_field_name arithmetic_operator INNR . int_field_name |
	INNR2 . char_field_name arithmetic_operator INNR . char_field_name ;

outer_condition_top:
	outer_condition_bottom |
	( outer_condition_bottom logical_operator outer_condition_bottom ) |
	outer_condition_bottom logical_operator outer_condition_bottom ;

outer_condition_bottom:
	OUTR . expression ;

expression:
	field_name null_operator |
	int_field_name int_expression |
	char_field_name char_expression ;

int_expression:
	arithmetic_operator digit ;

char_expression:
	arithmetic_operator _char(1);

inner_condition_top:
	INNR . expression |
	OUTR . expression |
	inner_condition_bottom logical_operator inner_condition_bottom |
	inner_condition_bottom logical_operator outer_condition_bottom ;

inner_condition_bottom:
	INNR . expression |
	INNR . int_field_name arithmetic_operator INNR . int_field_name |
	INNR . char_field_name arithmetic_operator INNR . char_field_name;

null_operator: IS NULL | IS NOT NULL ;

logical_operator: AND | OR | OR NOT;

logical_operator_disabled_bug37899: XOR;

logical_operator_disabled_bug37896: AND NOT ;

arithmetic_operator: = | > | < | <> | >= | <= ;

subquery_expression:
	EXISTS ( select_inner ) |
	NOT EXISTS ( select_inner ) |
	OUTR . field_name IN ( select_inner ) |
	( OUTR . field_name , OUTR . field_name ) IN ( select_inner_two_cols ) |
	OUTR . field_name NOT IN ( select_inner ) |
	( OUTR . field_name , OUTR . field_name ) NOT IN ( select_inner_two_cols ) |
	OUTR . field_name arithmetic_operator ( select_inner_one_row ) |
	OUTR . field_name arithmetic_operator subquery_word ( select_inner );

subquery_expression_disabled_crash_in_item_subselect:
	value IN ( select_inner );

subquery_expression_disabled_bug37894:
	value NOT IN ( select_inner );

subquery_word: SOME | ANY | ALL ;

field_name:
	int_field_name | char_field_name;

field_name_disabled_Field_newdate_assert: date_field_name;

# dates are disabled as they aren't in the
# gendata file (drizzle.zz)
date_field_name_disabled:
        `col_date_key` | `col_date_nokey` ;

date_field_name_disabled_Field_datetime_assert:
        `col_datetime_key` | `col_datetime_nokey` ;

date_field_name_disabled_convert_const_item:
	`col_time_key` ,
        `col_time_nokey` ;

int_field_name:
    `pk` | `col_int_key` | `col_int` |
    `col_bigint` | `col_bigint_key` |
    `col_int_not_null` | `col_int_not_null_key` ;

char_field_name:
        `col_char` | `col_text_not_null` | `col_text_not_null_key` |
        `col_text_key` | `col_text` | `col_char_not_null_key` | `col_char_not_null` ;

char_field_name_disabled:
# need to explore enum more before enabling this
        `col_enum` | `col_enum_key` | `col_enum_not_null` | `col_enum_not_null_key` ;

outer_table_name:
        AA | BB | small_table ;

inner_table_name:
        BB | CC | DD | small_table ;

small_table:
        A | B | C | C | C | D | D | D ;

value: _digit | _date | _time | _datetime | _char(1) | NULL ;

select_option: | ;

select_option_disabled_triggers_materialization_assert:
	| DISTINCT ;
