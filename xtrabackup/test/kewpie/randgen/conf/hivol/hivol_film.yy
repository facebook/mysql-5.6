query_init:
  USE hivol ;

query:
   { @nonaggregates = () ; $tables = 0 ; $fields = 0 ; ""}
   mlens_seed  ;

##############################################################################
# mlens_rules: rules related to the mlens-sourced tables
##############################################################################

mlens_seed:
   mlens_movie_root ;

mlens_movie_root:
  mlens_movies_with_tag | mlens_movies_with_tag | mlens_movie_genre ;

mlens_movies_with_tag:
  { $mlens_movie_alias='mwt_t1'; $mlens_tag_alias='mwt_t2'; "" }
  SELECT distinct straight_join select_option mlens_movie_select_list opt_mlens_tag_select_list
  FROM mlens_movie AS mwt_t1  left_right_outer JOIN mlens_tag  AS mwt_t2 
     ON { $mlens_movie_alias }  . `id` = { $mlens_tag_alias } . `mlens_movie_id` 
  WHERE { $mlens_tag_alias} . `tag` IS not NULL
  AND ( mlens_movie_where_list ) AND ( mlens_tag_where_list ) 
  optional_group_by
  ORDER BY total_order_by LIMIT limit_value ; 

mlens_movie_genre:
  mlens_movie_genre1 | mlens_movie_genre2 | 
  mlens_movie_genre3 | mlens_movie_genre3 | mlens_movie_genre3 ;

mlens_movie_genre1:
   { $mlens_movie_alias='mmg_t1'; $mlens_genre_alias='mmg_t2'; $mlens_rating_alias='mmg_t3'; "" }
  SELECT distinct straight_join select_option 
      mlens_movie_select_list opt_mlens_genre_select_list opt_mlens_rating_select_list
  FROM mlens_movie AS mmg_t1
       left_right_outer JOIN mlens_genre AS mmg_t2 
       ON { $mlens_movie_alias } . `id` = { $mlens_genre_alias} . `mlens_movie_id`
       left_right_outer JOIN mlens_rating AS mmg_t3 
       ON { $mlens_movie_alias } . `id` = { $mlens_rating_alias} . `mlens_movie_id`
  WHERE { $mlens_genre_alias } . `genre`  IN ( mlens_genre_list )
  AND ( mlens_movie_where_list )
  AND ( mlens_rating_where_list )
  opt_mlens_genre_where_list
  optional_group_by
  ORDER BY total_order_by LIMIT limit_value  ; 

mlens_movie_genre2:
  { $mlens_movie_alias='mmg_t1'; $mlens_genre_alias='mmg_t2'; $mlens_rating_alias='mmg_t3'; "" }
  SELECT distinct straight_join select_option 
      mlens_movie_select_list opt_mlens_genre_select_list opt_mlens_rating_select_list , aggregate( `rating` )
  FROM mlens_movie AS mmg_t1 
       left_right_outer JOIN mlens_genre AS mmg_t2 
       ON { $mlens_movie_alias } . `id` = { $mlens_genre_alias} . `mlens_movie_id`
       left_right_outer JOIN mlens_rating AS mmg_t3 
       ON { $mlens_movie_alias } . `id` = { $mlens_rating_alias} . `mlens_movie_id`
  WHERE { $mlens_genre_alias } . `genre`  IN ( mlens_genre_list )
  AND ( mlens_movie_where_list )
  AND ( mlens_rating_where_list )
  opt_mlens_genre_where_list
  group_by_clause
  ORDER BY total_order_by LIMIT limit_value ; 

mlens_movie_genre3:
  { $mlens_movie_alias='mmg_t1'; $mlens_genre_alias='mmg_t2'; $mlens_rating_alias='mmg_t3'; "" }
  SELECT distinct straight_join select_option mlens_movie_select_list opt_mlens_genre_select_list , COUNT( `genre` ) 
  FROM mlens_movie AS mmg_t1 
       left_right_outer JOIN mlens_genre AS mmg_t2 
       ON { $mlens_movie_alias } . `id` = { $mlens_genre_alias} . `mlens_movie_id`
  WHERE { $mlens_genre_alias } . `genre`  IN ( mlens_genre_list )
  AND ( mlens_movie_where_list )
  AND ( mlens_genre_where_list )
  group_by_clause 
  ORDER BY total_order_by LIMIT limit_value ;


##############################################################################
# mlens_movie_rules
##############################################################################

mlens_movie_select_list:
  mlens_movie_select_item | mlens_movie_select_item | mlens_movie_select_item, mlens_movie_select_list ;

mlens_movie_select_item:
  { $mlens_movie_alias } . mlens_movie_field AS { my $f = "field".++$fields ; push @nonaggregates , $f ; $f } ;

mlens_movie_field:
  `id` | `title` | `year` | `id` | `title` | `year` | 
  `alternate_title` ;

opt_mlens_movie_where_list:
 | and_or mlens_movie_where_list ;

mlens_movie_where_list:
  std_mlens_movie_where_list | std_mlens_movie_where_list | spec_mlens_movie_where_list ;

std_mlens_movie_where_list:
  mlens_movie_where_item |  std_mlens_movie_where_list and_or mlens_movie_where_item  ;

spec_mlens_movie_where_list:
  mlens_movie_sort_union_where_list ;

mlens_movie_sort_union_where_list:
  mlens_movie_where_item | mlens_movie_sort_union_where_list OR mlens_movie_where_item ;

mlens_movie_where_item:
  { $mlens_movie_alias } . mlens_movie_text_field not LIKE first_letter |
  { $mlens_movie_alias } . mlens_movie_text_field comparison_operator char_value |
  { $mlens_movie_alias } . mlens_movie_text_field comparison_operator char_value |
  { $mlens_movie_alias } . mlens_movie_text_field comparison_operator char_value |
  { $mlens_movie_alias } . mlens_movie_text_field comparison_operator char_value |
  { $mlens_movie_alias } . `year` comparison_operator year_value |
  { $mlens_movie_alias } . `year` comparison_operator year_value |
  { $mlens_movie_alias } . `year` comparison_operator year_value | 
  ( { $mlens_movie_alias } . `year` greater_than _year[invariant] AND { $mlens_movie_alias } . `year` less_than (_year[invariant] + small_increment) ) |
  { $mlens_movie_alias } . mlens_movie_text_field IS not NULL | 
  { $mlens_movie_alias } . mlens_movie_text_field IS not NULL | 
  { $mlens_movie_alias } . mlens_movie_text_field IS not NULL |
  LENGTH( { $mlens_movie_alias } . mlens_movie_text_field ) comparison_operator mlens_movie_text_length_sub ;

mlens_movie_text_field:
  `title` | `alternate_title` ;

##############################################################################
# mlens_tag rules
##############################################################################

opt_mlens_tag_select_list:
  | | | | | , mlens_tag_select_list ;

mlens_tag_select_list:
  mlens_tag_select_item | mlens_tag_select_item | mlens_tag_select_item, mlens_tag_select_list ;

mlens_tag_select_item:
  { $mlens_tag_alias } . mlens_tag_field AS { my $f = "field".++$fields ; push @nonaggregates , $f ; $f } ;

mlens_tag_field:
  `mlens_movie_id` | `user_id` | `tag` |
  `mlens_movie_id` | `user_id` | `tag` | `timestamp`  ;

opt_mlens_tag_where_list:
 | and_or mlens_tag_where_list ;

mlens_tag_where_list:
  std_mlens_tag_where_list | std_mlens_tag_where_list | spec_mlens_tag_where_list ;

std_mlens_tag_where_list:
  mlens_tag_where_item |  std_mlens_tag_where_list and_or mlens_tag_where_item  ;

spec_mlens_tag_where_list:
  mlens_tag_sort_union_where_list ;

mlens_tag_sort_union_where_list:
  mlens_tag_where_item | mlens_tag_sort_union_where_list OR mlens_tag_where_item ;

mlens_tag_where_item:
  { $mlens_tag_alias } . `tag` IS not NULL |
  { $mlens_tag_alias } . `tag` IS not NULL |
  { $mlens_tag_alias } . `tag` IS not NULL |
  { $mlens_tag_alias } . `tag` comparison_operator char_value |
  { $mlens_tag_alias } . `tag` comparison_operator char_value |
  { $mlens_tag_alias } . `tag` comparison_operator char_value |
  { $mlens_tag_alias } . `tag` not LIKE first_letter |
  { $mlens_tag_alias } . `timestamp` comparison_operator _timestamp |
  { $mlens_tag_alias } . `user_id` comparison_operator _smallint_unsigned | 
  { $mlens_tag_alias } . `timestamp` comparison_operator _timestamp |
  { $mlens_tag_alias } . `user_id` comparison_operator _smallint_unsigned |
  ( { $mlens_tag_alias } . `user_id` BETWEEN _smallint_unsigned[invariant] AND (_smallint_unsigned[invariant]+ increment) )|
  LENGTH(  { $mlens_tag_alias } . `tag` ) comparison_operator mlens_tag_text_length_sub ;

mlens_tag_text_field:
  `tag` ;

##############################################################################
# mlens_genre rules
##############################################################################
opt_mlens_genre_select_list:
  | | | | | , mlens_genre_select_list ;

mlens_genre_select_list:
  mlens_genre_select_item | mlens_genre_select_item | mlens_genre_select_item, mlens_genre_select_list ;

mlens_genre_select_item:
  { $mlens_genre_alias } . mlens_genre_field AS { my $f = "field".++$fields ; push @nonaggregates , $f ; $f } ;

mlens_genre_field:
  `mlens_movie_id` | `genre` ;

mlens_genre_list:
   mlens_genre_type | mlens_genre_list , mlens_genre_type ;

mlens_genre_type:
  'Action' | 'Adventure' | 'Animation' | 'Children' | 'Comedy' | 'Crime' |
  'Documentary' | 'Drama' | 'Fantasy' | 'Film-Noir' | 'Horror' | 'Musical' | 
  'Mystery' | 'Romance' | 'Sci-Fi' | 'Thriller' | 'War' | 'Western' ;

opt_mlens_genre_where_list:
 | and_or mlens_genre_where_list ;

mlens_genre_where_list:
  std_mlens_genre_where_list | std_mlens_genre_where_list | spec_mlens_genre_where_list ;

std_mlens_genre_where_list:
  mlens_genre_where_item |  std_mlens_genre_where_list and_or mlens_genre_where_item  ;

spec_mlens_genre_where_list:
  mlens_genre_sort_union_where_list ;

mlens_genre_sort_union_where_list:
  mlens_genre_where_item | mlens_genre_sort_union_where_list OR mlens_genre_where_item ;

mlens_genre_where_item:
  { $mlens_genre_alias } . `genre` comparison_operator mlens_genre_type |
  { $mlens_genre_alias } . `genre` comparison_operator char_value |
  { $mlens_genre_alias } . `genre` BETWEEN char_value AND char_value |
  { $mlens_genre_alias } . `genre` comparison_operator mlens_genre_type |
  { $mlens_genre_alias } . `genre` comparison_operator char_value |
  { $mlens_genre_alias } . `genre` BETWEEN char_value AND char_value |
  { $mlens_genre_alias } . `genre` comparison_operator mlens_genre_type |
  { $mlens_genre_alias } . `genre` comparison_operator char_value |
  { $mlens_genre_alias } . `genre` LIKE first_letter ;

##############################################################################
# mlens_rating rules
##############################################################################
opt_mlens_rating_select_list:
  | | | | | , mlens_rating_select_list ;

mlens_rating_select_list:
  mlens_rating_select_item | mlens_rating_select_item | mlens_rating_select_item, mlens_rating_select_list ;

mlens_rating_select_item:
  { $mlens_rating_alias } . mlens_rating_field AS { my $f = "field".++$fields ; push @nonaggregates , $f ; $f } ;

mlens_rating_field:
  `mlens_movie_id` | `user_id` | `rating` | `timestamp` ;


opt_mlens_rating_where_list:
  | and_or mlens_rating_where_list ;

mlens_rating_where_list:
  mlens_rating_where_item |  mlens_rating_where_list and_or mlens_rating_where_item  ;

mlens_rating_where_item:
  { $mlens_rating_alias } . `rating` comparison_operator mlens_rating_value |
  { $mlens_rating_alias } . `rating` IN ( mlens_rating_list ) |
  ( { $mlens_rating_alias } . `rating` greater_than mlens_rating_low AND { $mlens_rating_alias } . `rating` less_than mlens_rating_high ) ;
  { $mlens_rating_alias } . `rating` comparison_operator mlens_rating_value |
  { $mlens_rating_alias } . `rating` IN ( mlens_rating_list ) |
  { $mlens_rating_alias } . `rating` comparison_operator mlens_rating_value |
  { $mlens_rating_alias } . `rating` IN ( mlens_rating_list ) |
  { $mlens_rating_alias } . `timestamp` comparison_operator _timestamp |
  { $mlens_rating_alias } . `user_id` comparison_operator _smallint_unsigned | 
  { $mlens_rating_alias } . `user_id` BETWEEN _smallint_unsigned[invariant] AND (_smallint_unsigned[invariant]+ increment) ;

mlens_rating_list:
   mlens_rating_value | mlens_rating_list , mlens_rating_value ;

mlens_rating_value:
  0 | 0.5 | 1.0 | 1.5 | 2.0 | 2.5 |
  3.0 | 3.5 | 4.0 | 4.5 | 5.0 ;

mlens_rating_low:
 0 | 0.5 | 1.0 | 1.5 ;

mlens_rating_high:
  4.0 | 4.5 | 5.0 ;
##############################################################################
# fbase film rules: rules related to fbase-sourced queries
##############################################################################
fbase_seed:
  fbase_film_series ;

fbase_film_series:
  SELECT fbase_film_detail.id, fbase_film_detail.name
  FROM fbase_film_series AS fbs_t1 { $fbase_film_series_alias='fbs_t1' ; "" }
    JOIN fbase_film_detail AS fbs_t2 { $fbase_film_detail_alias='fbs_t2' ; "" }
    ON fbase_film_series . fbase_film_detail_id = fbase_film_detail . id 
    JOIN mlens_movie_with_rating_parm_sub AS mlens_derived_t1 
    ON fbase_film_detail . name = mlens_derived_t1 . title
  WHERE fbase_film_series_where_list and_or fbase_film_detail_where_list 
  ORDER BY 1 LIMIT limit_value ;

##############################################################################
# fbase_series rules
##############################################################################
opt_fbase_film_series_where_list:
 | and_or fbase_film_series_where_list ;

fbase_film_series_where_list:
  std_fbase_film_series_where_list | std_fbase_film_series_where_list | spec_fbase_film_series_where_list ;

std_fbase_film_series_where_list:
  fbase_film_series_where_item |  fbase_series_where_item | 
  std_fbase_film_series_where_list and_or fbase_film_series_where_item  ;

spec_fbase_film_series_where_list:
  fbase_film_series_sort_union_where_list ;

fbase_film_series_sort_union_where_list:
  fbase_film_series_where_item | fbase_film_series_where_item | fbase_film_series_sort_union_where_list OR fbase_film_series_where_item ;

fbase_film_series_where_item:
  { $fbase_film_series_alias } . sequel_flag = zero_one ;

##############################################################################
# fbase_film_detail rules
##############################################################################
opt_fbase_film_detail_where_list:
 | and_or fbase_film_detail_where_list ;

fbase_film_detail_where_list:
  std_fbase_film_detail_where_list | std_fbase_film_detail_where_list | spec_fbase_film_detail_where_list ;

std_fbase_film_detail_where_list:
  fbase_film_detail_where_item |  fbase_series_where_item | 
  std_fbase_film_detail_where_list and_or fbase_film_detail_where_item  ;

spec_fbase_film_detail_where_list:
  fbase_film_detail_sort_union_where_list ;

fbase_film_detail_sort_union_where_list:
  fbase_film_detail_where_item | fbase_film_detail_where_item | 
  fbase_film_detail_sort_union_where_list OR fbase_film_detail_where_item ;

fbase_film_detail_where_item:
  { $fbase_film_detail_alias } . init_release_date comparison_operator _datetime |
  { $fbase_film_detail_alias } . est_budget_id IS not NULL |
  { $fbase_film_detail_alias } . name comparison_operator char_value ;  



##############################################################################
# subqueries
##############################################################################
# value subqueries: return a value that is used in the main query's WHERE clause

mlens_movie_text_length_sub:
# rule for selecting the MIN/MAX/AVG length of a text field from mlens_movie
  ( SELECT size_aggregate( distinct LENGTH( mlens_movie_text_field ) ) FROM mlens_movie ) ;

mlens_tag_text_length_sub:
  ( SELECT size_aggregate( distinct LENGTH( mlens_tag_text_field ) ) FROM mlens_tag ) ;


mlens_movie_with_rating_parm_sub:
# rule for selecting those movies that have enough ratings of a certain condition
  ( SELECT distinct straight_join select_option mlens_movie.id, mlens_movie.title 
    FROM mlens_movie { $mlens_movie_alias='mlens_movie'; "" } left_right_outer JOIN 
    ( SELECT mlens_movie_id, COUNT(rating) FROM mlens_rating 
      WHERE mlens_rating_where_list  
      GROUP BY mlens_movie_id 
      HAVING COUNT(rating) > numeric_value ) AS mlens_rating_t1 
    ON id = mlens_movie_id ) ;


##############################################################################
# value rules
# rules that contain lists of possible values for a comparison, etc
##############################################################################

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

zero_one:
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

