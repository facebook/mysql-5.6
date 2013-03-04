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

################################################################################
# NOTE:  This is an older grammar and should not be prioritized over other 
#        grammars (such as optimizer*subquery, outer_join, range_access)
#        While this is still an effective testing tool, the grammars listed
#        above should be run first 
################################################################################

query:
	SELECT select_option grandparent_select_items
	FROM grandparent_from
	grandparent_where 
	grandparent_group_having_order_limit;

grandparent_select_items:
	grandparent_select_item;

grandparent_select_item:
	GRANDPARENT1 . field_name AS g1;

grandparent_from:
	table_name AS GRANDPARENT1 |
	table_name AS GRANDPARENT1 LEFT JOIN table_name AS GRANDPARENT2 USING ( field_name ) |
	table_name AS GRANDPARENT1 LEFT JOIN table_name AS GRANDPARENT2 ON ( grandparent_join_condition );

grandparent_join_condition:
	GRANDPARENT2 . int_field_name arithmetic_operator GRANDPARENT1 . int_field_name |
	GRANDPARENT2 . date_field_name arithmetic_operator GRANDPARENT1 . date_field_name |
	GRANDPARENT2 . char_field_name arithmetic_operator GRANDPARENT1 . char_field_name ;

grandparent_where:
	WHERE grandparent_subquery_expr AND grandparent_condition ;

grandparent_subquery_expr:
	GRANDPARENT1 . int_field_name IN ( SELECT select_option PARENT1 . int_field_name AS p1 parent_select_body ) |
	GRANDPARENT1 . char_field_name IN ( SELECT select_option PARENT1 . char_field_name AS p1 parent_select_body ) |
	( GRANDPARENT1 . int_field_name , GRANDPARENT1 . int_field_name ) IN ( SELECT select_option PARENT1 . int_field_name AS p1 , PARENT1 . int_field_name AS p2 parent_select_body ) |
	( GRANDPARENT1 . char_field_name , GRANDPARENT1 . char_field_name ) IN ( SELECT select_option PARENT1 . char_field_name AS p1 , PARENT1 . char_field_name AS p2 parent_select_body ) ;

grandparent_group_having_order_limit:
	grandparent_group_by grandparent_having grandparent_order_by grandparent_limit |
	grandparent_group_by grandparent_having grandparent_limit |
	grandparent_having grandparent_order_by grandparent_limit |
	grandparent_having ;

grandparent_group_by:
	GROUP BY GRANDPARENT1 . field_name ;

grandparent_order_by:
	ORDER BY GRANDPARENT1 . field_name ;

grandparent_having:
	| HAVING g1 arithmetic_operator value;

grandparent_limit:
	| LIMIT digit ;

parent_select_body:
	FROM parent_from
	parent_where
	parent_order_by;

parent_from:
	table_name AS PARENT1 |
	table_name AS PARENT1 LEFT JOIN table_name AS PARENT2 USING ( field_name ) |
	table_name AS PARENT1 LEFT JOIN table_name AS PARENT2 ON ( parent_join_condition ) ;

parent_join_condition:
	PARENT1 . int_field_name arithmetic_operator PARENT2 . int_field_name |
	PARENT1 . char_field_name arithmetic_operator PARENT2 . char_field_name ;

parent_where:
	| WHERE parent_subquery_expr AND parent_condition
	| WHERE parent_condition ;

parent_order_by:
	| ORDER BY PARENT1 . field_name ;

parent_subquery_expr:
	PARENT1 . int_field_name IN ( SELECT select_option CHILD1 . int_field_name AS C1 child_select_body ) |
	GRANDPARENT1 . int_field_name IN ( SELECT select_option CHILD1 . int_field_name AS C1 child_select_body ) |

	PARENT1 . char_field_name IN ( SELECT select_option CHILD1 . char_field_name AS C1 child_select_body ) |
	GRANDPARENT1 . char_field_name IN ( SELECT select_option CHILD1 . char_field_name AS C1 child_select_body ) |

	( PARENT1 . int_field_name , PARENT1 . int_field_name ) IN ( SELECT select_option CHILD1 . int_field_name AS C1 , CHILD1 . int_field_name AS C2 child_select_body ) |
	( PARENT1 . int_field_name , GRANDPARENT1 . int_field_name ) IN ( SELECT select_option CHILD1 . int_field_name AS C1 , CHILD1 . int_field_name AS C2 child_select_body ) |

	( PARENT1 . char_field_name , PARENT1 . char_field_name ) IN ( SELECT select_option CHILD1 . char_field_name AS C1 , CHILD1 . char_field_name AS C2 child_select_body ) ;
	( PARENT1 . char_field_name , GRANDPARENT1 . char_field_name ) IN ( SELECT select_option CHILD1 . char_field_name AS C1 , CHILD1 . char_field_name AS C2 child_select_body ) ;

	( PARENT1 . char_field_name , PARENT1 . char_field_name ) IN ( SELECT select_option CHILD1 . char_field_name AS C1 , CHILD1 . char_field_name AS C2 child_select_body ) ;
	( PARENT1 . int_field_name , PARENT1 . int_field_name ) IN ( SELECT select_option CHILD1 . int_field_name AS C1 , CHILD1 . int_field_name AS C2 child_select_body ) ;

child_select_body:
	FROM child_from
	child_where
	child_order_by;

child_from:
	table_name AS CHILD1 |
	table_name AS CHILD1 LEFT JOIN table_name AS CHILD2 USING ( field_name ) |
	table_name AS CHILD1 LEFT JOIN table_name AS CHILD2 ON ( child_join_condition ) ;

child_join_condition:
	CHILD1 . int_field_name arithmetic_operator CHILD2 . int_field_name |
	CHILD1 . char_field_name arithmetic_operator CHILD2 . char_field_name ;

child_where:
	| WHERE child_condition ;

child_order_by:
	| ORDER BY CHILD1 . field_name ;

child_condition:
	( GRANDPARENT1 . expression ) |
	( PARENT1 . expression ) |
	( CHILD1 . expression ) |
	( child_condition_bottom logical_operator child_condition ) |
	( child_condition_bottom logical_operator parent_condition ) |
	( child_condition_bottom logical_operator grandparent_condition ) ;

child_condition_bottom:

	( CHILD1 . expression ) |

	( CHILD1 . int_field_name arithmetic_operator CHILD1 . int_field_name ) |
	( CHILD1 . date_field_name arithmetic_operator CHILD1 . date_field_name ) |
	( CHILD1 . char_field_name arithmetic_operator CHILD1 . char_field_name ) |

	( CHILD1 . int_field_name arithmetic_operator PARENT1 . int_field_name ) |
	( CHILD1 . date_field_name arithmetic_operator PARENT1 . date_field_name ) |
	( CHILD1 . char_field_name arithmetic_operator PARENT1 . char_field_name ) |

	( CHILD1 . int_field_name arithmetic_operator GRANDPARENT1 . int_field_name ) |
	( CHILD1 . date_field_name arithmetic_operator GRANDPARENT1 . date_field_name ) |
	( CHILD1 . char_field_name arithmetic_operator GRANDPARENT1 . char_field_name ) ;

grandparent_condition:
	grandparent_condition_bottom |
	( grandparent_condition logical_operator grandparent_condition_bottom ) ;

grandparent_condition_bottom:
	GRANDPARENT1 . expression ;
	
expression:
	field_name null_operator |
	int_field_name int_expression |
	date_field_name date_expression |
	char_field_name char_expression ;

int_expression:
	arithmetic_operator digit ;

date_expression:
	arithmetic_operator date | BETWEEN date AND date;

char_expression:
	arithmetic_operator _varchar(1);

parent_condition:
	( GRANDPARENT1 . expression ) |
	( PARENT1 . expression ) |
	( parent_condition_bottom logical_operator parent_condition ) |
	( parent_condition_bottom logical_operator grandparent_condition ) ;

parent_condition_bottom:
	( PARENT1 . expression ) |

	( PARENT1 . int_field_name arithmetic_operator PARENT1 . int_field_name ) |
	( PARENT1 . date_field_name arithmetic_operator PARENT1 . date_field_name ) |
	( PARENT1 . char_field_name arithmetic_operator PARENT1 . char_field_name ) |

	( PARENT1 . int_field_name arithmetic_operator GRANDPARENT1 . int_field_name ) |
	( PARENT1 . date_field_name arithmetic_operator GRANDPARENT1 . date_field_name ) |
	( PARENT1 . char_field_name arithmetic_operator GRANDPARENT1 . char_field_name ) ;

null_operator: IS NULL | IS NOT NULL | IS UNKNOWN ;

logical_operator:
	AND | OR | OR NOT;

arithmetic_operator: = | > | < | <> | >= | <= ;

field_name:
	int_field_name | char_field_name | date_field_name;

int_field_name:
        `pk` | `col_int_key` | `col_int_nokey` ;

date_field_name:
        `col_date_key` | `col_date_nokey` | `col_datetime_key` | `col_datetime_nokey` |	`col_time_key` | `col_time_nokey` ;

char_field_name:
        `col_varchar_key` | `col_varchar_nokey` ;

table_name:
	B | C | BB | CC;

value: _digit | _date | _time | _datetime | _varchar(1) | NULL ;

select_option: 
	| DISTINCT ;
