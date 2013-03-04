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

# Note: usage of date/time columns has been removed
# for the time being.  Feel free to experiment to
# add them back in later.  This would require altering
# the gendata file, conf/drizzle/drizzle.zz

# meant to be run with the gendata file: conf/drizzle/drizzle.zz

query:
	SELECT select_option outer_select_item
	FROM outer_from
	WHERE subquery_expression AND outer_condition_top
	outer_having outer_order_by ;

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
	VAR_POP( | VAR_SAMP( | VARIANCE( |
	AVG(DISTINCT ;

aggregate_function_disabled_unpredictable_value:
	GROUP_CONCAT( | ;

outer_from:
	outer_table_name AS OUTR |
	outer_table_name AS OUTR2 LEFT JOIN outer_table_name AS OUTR ON ( outer_join_condition );

outer_join_condition:
	OUTR2 . int_field_name arithmetic_operator OUTR . int_field_name |
	OUTR2 . char_field_name arithmetic_operator OUTR . char_field_name ;

outer_order_by:
	ORDER BY OUTR . field_name , OUTR . `pk` ;

outer_group_by:
	| GROUP BY OUTR . field_name ;

outer_having:
	| HAVING X arithmetic_operator value;

outer_having_disabled_bug38072:
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

inner_order_by:
	| ORDER BY INNR . field_name ;

inner_select_item:
	INNR . int_field_name AS Y ;

inner_from:
	inner_table_name AS INNR |
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
	arithmetic_operator _varchar(1);

inner_condition_top:
	INNR . expression |
	OUTR . expression |
	inner_condition_bottom logical_operator inner_condition_bottom |
	inner_condition_bottom logical_operator outer_condition_bottom ;

inner_condition_bottom:
	INNR . expression |
	INNR . int_field_name arithmetic_operator INNR . int_field_name |
	INNR . char_field_name arithmetic_operator INNR . char_field_name ;

null_operator: IS NULL | IS NOT NULL ;

logical_operator:
	AND | OR | OR NOT | XOR | AND NOT ;

arithmetic_operator:
	= | > | < | <> | >= | <= ;

subquery_expression:
	OUTR . int_field_name IN ( SELECT select_option INNR . int_field_name AS Y select_inner_body ) |
	OUTR . char_field_name IN ( SELECT select_option INNR . char_field_name AS Y select_inner_body ) |
	( OUTR . int_field_name , OUTR . int_field_name ) IN ( SELECT select_option INNR . int_field_name AS X , INNR . int_field_name AS Y select_inner_body ) |
	( OUTR . char_field_name , OUTR . char_field_name ) IN ( SELECT select_option INNR . char_field_name AS X , INNR . char_field_name AS Y select_inner_body ) ;

field_name:
	int_field_name | char_field_name ;

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

outer_table_name:
	AA | BB | AA | BB | C | D | CC | DD | A ;

inner_table_name:
	BB | CC | DD | BB | CC | DD | C | D | A ;

value: _digit | _date | _time | _datetime | _varchar(1) | NULL ;

select_option: 
	| DISTINCT ;
