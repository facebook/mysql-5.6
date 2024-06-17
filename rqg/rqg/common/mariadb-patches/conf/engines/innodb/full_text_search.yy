# Copyright (c) 2012 Oracle and/or its affiliates. All rights reserved.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software Foundation,
# 51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA


################################################################################
# full_text_search.yy
# Purpose:  Grammar for testing fulltext search condition
#
# Notes:    This grammar is designed to be used with 
#           gendata=conf/engines/innodb/full_text_search.zz
#           Fulltext serach condition can be used on the column on which fulltext
#           index is defined. 
#
#           IMP : Pass '--innodb_ft_enable_stopword=0' to server , this allows
#           user to search default stopword. ( english.txt contain stopwords
#           so these words will not be searched if don't pass this argument.)  
#
#           There are 3 types for search conditions - natural,binary,query 
#           expansion mode. Innodb has added proximity search feature (belong to 
#           binary mode) which allows search based on distance between the words.
#           This type of query generation is disabled in grammar as it required 
#           small change in rqg.(it requires unquoted english words from rqg)  
#
#           We keep the grammar here as it is in order to also test certain
#           MySQL-specific syntax variants.
################################################################################

# Run Select , DML , DDL , transactional statements
query:
     select | select | select | select | select | update | delete | 
     create_drop_index | transaction | insert | insert ;

# Add/Drop fulltext index 
create_drop_index:
     ALTER TABLE _table ADD FULLTEXT INDEX {"idx_". $indexcount++} (_field_no_pk) 
     | ALTER TABLE _table DROP INDEX {"idx_".$prng->int(1,$indexcount)} ;

# Statement to start or to end transaction.
transaction:
     START TRANSACTION | COMMIT | ROLLBACK ;

# Prepeare 3 type of fulltext search queries condition
# Enable proximity search when its possinle to get string without quote
select:
#     natural_language_search | boolean_search | proximity_search 
#     | query_expansion_search ;
     natural_language_search | boolean_search | query_expansion_search ;

# Type - Natural language Search queries with 
# SELECT .. MATCH (<fields>) AGAING ( <string> IN NATURAL LANGUAGE MODE )
natural_language_search:
     SELECT select_list FROM _table WHERE natural_language_search_condition expression | 
     SELECT _field_indexed[invariant],natural_language_search_condition AS SCORE FROM _table WHERE natural_language_search_condition expression order_clause |
     SELECT _field_indexed[invariant],natural_language_search_condition AS SCORE FROM _table order_clause limit_clause;

natural_language_search_condition:
     MATCH (_field_no_pk[invariant]) AGAINST (_english[invariant] IN NATURAL LANGUAGE MODE );

# Type - Boolean Search queries with, 
# SELECT .. MATCH (<fields>) AGAING ( <string> IN BOOLEAN MODE )
boolean_search:
     SELECT select_list FROM _table WHERE boolean_search_condition expression | 
     SELECT _field_indexed[invariant],boolean_search_condition AS SCORE FROM _table WHERE boolean_search_condition expression order_clause |
     SELECT _field_indexed[invariant],boolean_search_condition AS SCORE FROM _table order_clause limit_clause;

boolean_search_condition:
     MATCH (_field_no_pk[invariant]) AGAINST ( CONCAT( concatinate_strings ) IN BOOLEAN MODE);

# Type - Query expansion mode , 
# SELECT .. MATCH (<fields>) AGAING ( <string> WITH QUERY EXPANSION )
query_expansion_search:
     SELECT select_list FROM _table WHERE query_expansion_search_condition | 
     SELECT _field_indexed[invariant],query_expansion_search_condition AS SCORE FROM _table WHERE query_expansion_search_condition expression order_clause |
     SELECT _field_indexed[invariant],query_expansion_search_condition AS SCORE FROM _table order_clause limit_clause; 

query_expansion_search_condition:
     MATCH (_field_no_pk[invariant]) AGAINST ( _english[invariant] WITH QUERY EXPANSION );

# Type - Proximity search - Innodb Feature , Search done with Boolean Mode
proximity_search:
     SELECT select_list FROM _table WHERE proximity_search_condition expression | 
     SELECT _field_indexed[invariant],proximity_search_condition AS SCORE FROM _table WHERE proximity_search_condition expression order_clause |
     SELECT _field_indexed[invariant],proximity_search_condition AS SCORE FROM _table order_clause limit_clause;

proximity_search_condition:
     MATCH (_field_no_pk[invariant]) AGAINST ( proximity_search_string IN BOOLEAN MODE);

proximity_search_string:
     single_quote double_quote _englishnoquote double_quote {$val= "@".$prng->int(0,15)} single_quote 
     | single_quote double_quote _englishnoquote _englishnoquote double_quote {$val= "@".$prng->int(0,15)} single_quote;

select_list:
     count(*) | * ;

expression:
    > 0 | = 0 | > 1 | < 1 | != 0 | != 1 | | | | | | | | | | |;   

order_clause:
     ORDER BY SCORE order_type | ORDER BY {$prng->int(1,2)} order_type | | |;

order_type:
     DESC | DESC | DESC | DESC | ASC | |;

limit_clause:
     LIMIT {$prng->int(0,3)} | LIMIT {$prng->int(0,3)} | LIMIT {$prng->int(10,30)};

get_string:
     _english[invariant];

concatinate_strings:
     get_string | str_with_operator 
     | single_quote ( single_quote , concatinate_strings , single_quote ) single_quote ;

str_with_operator:
     boolean_operators , get_string | get_string , boolean_operator_at_end_of_string ;

boolean_operator_at_end_of_string:
     '' | '' | '' | {$rval="'*'"};

boolean_operators:
     {$rval="'+'"} | {$rval="'-'"} | {$rval="'>'"} 
     | {$rval="'<'"} | {$rval="'~'"} ;
#    { $str = '+' . $prng->fieldType("_english") ; return($str) } ;

single_quote:
     ';

double_quote:
     ";

# Condition to be used in update and delete statement
condition:
#     natural_language_search_condition 
#     | boolean_search_condition 
#     | proximity_search_condition 
#     | query_expansion_search_condition ;
     natural_language_search_condition 
     | boolean_search_condition 
     | query_expansion_search_condition ;

insert :
    INSERT INTO _table ( _field_no_pk ) VALUES ( _english ) ;    

update:
    UPDATE _table SET _field_no_pk = _english WHERE condition ;
 
delete:
    DELETE FROM _table WHERE condition ;
