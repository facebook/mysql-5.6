query_init:
  USE hivol ;

query:
   { @nonaggregates = () ; $tables = 0 ; $fields = 0 ; ""}
   fbase_query ;

fbase_query:
   fbase_query1 ;

fbase_query1:
# will have series, film_crew, and genre
SELECT distinct straight_join select_option fbase_query1_select_list
FROM `fbase_film_series` AS table1 left_right_outer JOIN
`fbase_film_crew` AS table2
    ON table1 . fbase_film_detail_id = table2 . fbase_film_detail_id
left_right_outer JOIN `fbase_film_genre` AS table3 
    ON table_one_two . fbase_film_detail_id = table3 . fbase_film_detail_id
WHERE ( fbase_film_series_where_list ) 
and_or ( fbase_film_crew_where_list ) 
and_or ( fbase_film_genre_where_list )
opt_fbase_query1_having_clause
ORDER BY total_order_by LIMIT limit_value ;

fbase_query1_select_list:
  table1 . fbase_film_detail_id AS { my $f = "field".++$fields ; push @nonaggregates , $f ; $f } ,
  fbase_role_code AS { my $f = "field".++$fields ; push @nonaggregates , $f ; $f } ,
  genre_tag AS { my $f = "field".++$fields ; push @nonaggregates , $f ; $f } ;

fbase_select_disabled:
  fbase_film_series_select_list |
  fbase_film_genre_select_list |
  fbase_film_crew_select_list ;

fbase_film_series_select_list:
  'a' ;

opt_fbase_query1_having_clause:
 | | | | | | fbase_query1_having_clause ;  

fbase_query1_having_clause:
  HAVING fbase_query1_having_list ;

fbase_query1_having_list:
  fbase_film_series_where_list |
  fbase_film_genre_where_list |
  fbase_film_crew_where_list ;

fbase_film_series_where_list:
  fbase_film_series_where_item | 
  fbase_film_series_where_item and_or fbase_film_series_where_list ;

fbase_film_series_where_item:
  related_film_id comparison_operator char_value |
  ( related_film_id BETWEEN char_value AND char_value ) |
  related_film_id comparison_operator table_one_two_three . fbase_film_detail_id |
  sequel_flag comparison_operator true_false |
  sequel_flag = zero_one ;

fbase_film_genre_where_list:
  fbase_film_genre_where_item |
  fbase_film_genre_where_item and_or fbase_film_series_where_list |
  ( fbase_film_genre_where_item and_or fbase_film_series_where_list ) |
  ( fbase_film_genere_where_item ) and_or fbase_film_series_where_list ;

fbase_film_genre_where_item:
  genre_tag comparison_operator char_value |
  ( genre_tag BETWEEN char_value AND char_value ) |
  genre_tag LIKE first_letter ;
  genre_tag IN ( fbase_genre_list ) ;

fbase_genre_list:
  fbase_genre_item | fbase_genre_item , fbase_genre_list ;

fbase_genre_item:
  'Camp' | 'Comedy' | 'Crime' |  'Cyberpunk' | 
  'Horror' | 'History' | 'Martial arts' | 'Sports' ;


fbase_film_crew_where_list:
  fbase_film_crew_where_item |
  fbase_film_crew_where_item and_or fbase_film_series_where_list |
  ( fbase_film_crew_where_item and_or fbase_film_series_where_list ) |
  ( fbase_film_genere_where_item ) and_or fbase_film_series_where_list ;

fbase_film_crew_where_item:
  fbase_role_code LIKE first_letter |
  fbase_role_code comparison_operator char_value |
  LENGTH( fbase_role_code ) comparison_operator numeric_value | 
  ( fbase_role_code BETWEEN char_value AND char_value ) | 
  fbase_role_code IN ( fbase_role_list ) ;

fbase_role_list:
  fbase_role_item | fbase_role_item , fbase_role_list ;

fbase_role_item:
  'actor' | 'director' | 'producer' | 'writer' | 'editor' ;


  
  
  
   

##############################################################################
# value rules
# rules that contain lists of possible values for a comparison, etc
##############################################################################

true_false:
  TRUE | FALSE ;

zero_one:
  0 | 1 ;

table_one_two:
  table1 | table2 ;

table_one_two_three:
  table1 | table2 | table3 ;

year_value:
  _year | 1900 | 1910 | 1920 | 1930 |
   1940 | 1950 | 1960 | 1970 | 
   1975 | 1980 | 1985 | 1990 | 1995 ;

first_letter:
 'A%' | 'B%' | 'C%' | 'D%' | 'E%' | 'F%' | 'G%' | 'H%' | 'I%' | 'J%' | 'K%' | 'L%' | 'M%' | 'N%' |
 'P%' | 'Q%' | 'R%' | 'S%' | 'T%' | 'U%' | 'V%' | 'X%' | 'Y%' | 'Z%' ; 

char_value:
  _char | _english | _english | _quid | _quid ;

numeric_value:
  increment ;

small_increment:
  1 | 1 | 2 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 ;

large_increment:
  20 | 25 | 50 | 75 | 100 ;

increment:
  small_increment | large_increment ;

limit_value:
  1000 | 10000 | 5000 | 500 | 100 | 10 ;

:zero_one
 0 | 1 ;

##############################################################################
# general utility rules
##############################################################################
total_order_by:
	{ join(', ', map { "field".$_ } (1..$fields) ) } desc ;

group_by_clause:
	{ scalar(@nonaggregates) > 0 ? " GROUP BY ".join (', ' , @nonaggregates ) : "" }  ;

optional_group_by:
        | | | | | | | | | | group_by_clause ;

size_aggregate:
  MAX | MIN | AVG | 
  MAX | MIN | AVG | SUM ;

aggregate:
  MAX | MIN | SUM | COUNT ;

comparison_operator:
# NOTE: '=' is omitted here as we are using random
#       comparison values that are impossible to
#       have a match
   > | < | != | <> | <= | >= | greater_than | less_than ;

greater_than:
  > | >= ;

less_than:
  < | <= ;

left_right_outer:
  | | | | | | | LEFT outer | LEFT outer | RIGHT outer ;

left_right:
  | | | | LEFT | LEFT | LEFT | RIGHT ;

outer:
  | | OUTER ;

distinct:
  DISTINCT | | | | | | ;

select_option:  
  | | | | | | | | | SQL_SMALL_RESULT ;

straight_join:  
  | | | | | | | | | | | STRAIGHT_JOIN ;

not:
 | | | NOT ; 

and_or:
 AND | AND |  OR ;

desc:
        ASC | | | DESC ;

