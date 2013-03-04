query:
	INSERT INTO _table ( _field ) VALUE ( datetime_expr ) |
	INSERT INTO _table ( _field ) VALUE ( datetime_expr ) |
	INSERT INTO _table ( _field ) VALUE ( datetime_expr ) |
	INSERT INTO _table ( _field ) VALUE ( datetime_expr ) |
	UPDATE _table SET _field = datetime_expr WHERE where_list |
	DELETE FROM _table WHERE where_list ORDER BY pk LIMIT 1 ;

#	{ $col = 1 ; return "" }
#	SELECT select_list
#	FROM _table
#	WHERE where_list 
#	having 
#	order_by ;

select_list:
	select_item |
	select_item , select_list |
	select_item , select_item ;

select_item:
	_field AS { 'c'.$col++ } |
	datetime_expr AS { 'c'.$col++ } ;

where_list:
	where_item |
	where_item and_or where_list ;

where_item:
	_field not IN ( datetime_list ) |
#	_field not BETWEEN datetime_expr AND datetime_expr |
	_field comp_op datetime_expr |
	datetime_expr IS not NULL ;

order_by:
	| 
	ORDER BY field_list | 
	ORDER BY 1 ;

having:
	|
	HAVING datetime_expr ;

field_list:
	_field , field_list |
	_field , _field ; 

comp_op:
	= | < | > | != | <> | <=> | >= | <= ;

not:
	| NOT ;

and_or:
	AND | OR ;

datetime_list:
	datetime_expr , datetime_expr |
	datetime_expr , datetime_list ;

datetime_expr:
	datetime_func | datetime_field ;

arg_datetime_list:
	arg_datetime , arg_datetime |
	arg_datetime , arg_datetime_list ; 

arg_datetime:
	arg_datetime | arg_datetime | arg_datetime | arg_datetime | arg_date | datetime_func | '0000-00-00 00:00:00' | datetime_field ;

arg_date:
	_date | _date | _date | _date | '0000-00-00' | datetime_func | datetime_field ;

arg_time:
	_time | _time | _time | _time | '00:00:00' | datetime_func | datetime_field ;

arg_any_list:
	arg_any , arg_any |
	arg_any , arg_any_list ;

arg_any:
	arg_datetime | arg_time | arg_date | datetime_func | datetime_field ;

arg_integer:
	_tinyint_unsigned | digit | integer_func | integer_field ;

integer_field:
	_field ;

datetime_field:
	_field ;

arg_unix:
	_integer_unsigned ;

arg_tz:
	'MET' |
	'UTC' |
	'Universal' |
	'Europe/Moscow' |
	'leap/Europe/Moscow' |
	'Japan' |
	 CONCAT( plus_minus , CONCAT_WS(':', _digit , _digit ) ) ;

arg_hour:
	_digit | 24 | _tinyint_unsigned | integer_func | 24 ;

arg_minute:
	_digit | _tinyint_unsigned | integer_func | 60 ;

arg_second:
	_digit | _digit | _digit | _tinyint_unsigned | integer_func ;

arg_dayofyear:
	_tinyint_unsigned | _tinyint_unsigned | _tinyint_unsigned | integer_func ;

arg_year:
	19 + _digit | 20 + _digit | _tinyint_unsigned | integer_func | '0000';

arg_days:
	arg_integer ;

arg_formatted:
	DATE_FORMAT( arg_any , arg_format ) ;

plus_minus:
	'-' | '+' ;

datetime_func:
#	LEAST( arg_any_list ) |
#	GREATEST( arg_any_list ) |
#	COALESCE( arg_any_list ) |

#	CAST( arg_any AS arg_cast_type ) |
	date_add_sub |
	ADDDATE( arg_datetime , arg_days ) |
#	ADDTIME( arg_any , arg_time ) |
	CONVERT_TZ( arg_datetime , arg_tz , arg_tz ) |
	CURDATE() | CURRENT_DATE() |
#	CURTIME() | CURRENT_TIME() |
#	CURRENT_TIMESTAMP() | NOW() |
	DATE( arg_date ) | DATE ( arg_datetime ) |
	FROM_DAYS( arg_integer ) |
	FROM_UNIXTIME( arg_unix ) | FROM_UNIXTIME( arg_unix , arg_format ) |
#	LAST_DAY( arg_datetime ) |
#	LOCALTIME() | LOCALTIMESTAMP() |
	MAKEDATE( arg_year , arg_dayofyear ) |
#	MAKETIME( arg_hour , arg_minute, arg_second ) |
#	NOW() |
#	SEC_TO_TIME( arg_second ) |
#	STR_TO_DATE( arg_formatted , arg_format ) |
#	SYSDATE() |
#	TIME( arg_any ) |
#	TIMEDIFF( arg_any , arg_any ) |
	TIMESTAMP( arg_any ) | TIMESTAMP( arg_any , arg_time ) |
	TIMESTAMPADD( arg_unit_timestamp , arg_integer , arg_datetime ) |
#	UNIX_TIMESTAMP() |
#	UNIX_TIMESTAMP( arg_datetime ) |
	UTC_DATE() ;
#|
#	UTC_TIME() | UTC_TIMESTAMP() |
#	SUBTIME( arg_datetime , arg_time ) |
#	EXTRACT( arg_unit_noninteger FROM arg_any ) ;

integer_func:
	DATEDIFF( arg_date , arg_date ) | DATEDIFF( arg_datetime , arg_datetime ) |
	DAY( arg_date ) | DAYOFMONTH( arg_date ) ;
	DAYOFMONTH( arg_datetime ) |
	DAYOFWEEK( arg_datetime ) |
	DAYOFYEAR( arg_datetime ) |
	EXTRACT( arg_unit_integer FROM arg_any ) |
	HOUR( arg_datetime ) |	
	MICROSECOND( arg_any ) |
	MINUTE( arg_time ) |
	MONTH( arg_date ) |
	PERIOD_ADD( arg_period , arg_integer ) |
	PERIOD_DIFF( arg_period , arg_period ) |
	QUARTER( arg_date ) |
	SECOND( arg_any ) |
	TIMESTAMPDIFF( arg_interval ,  arg_datetime , arg_datetime ) |
	TIME_TO_SEC( arg_any ) |
	TO_DAYS( arg_datetime ) |
	TO_SECONDS( arg_datetime ) |
	WEEK( arg_datetime , arg_mode ) |
	WEEKDAY( arg_datetime ) |
	WEEKOFYEAR( arg_datetime ) |
	YEAR( arg_datetime ) ;
	YEARWEEK( arg_datetime ) | YEARWEEK( arg_datetime , arg_mode ) |

	IF( integer_func , datetime_func , datetime_func ) |
	IFNULL( datetime_func ) |
	NULLFIF( datetime_func, datetime_func ) |
	INTERVAL ( arg_datetime_list ) ;


string_func:
	DATE_FORMAT( arg_any , arg_format ) |
	DAYNAME( arg_date ) ;
	MONTHNAME( arg_date ) |
	TIME_FORMAT( arg_time , arg_time_format ) |


	SUBDATE( arg_date , INTERVAL arg_expr arg_unit ) |
	SUBDATE( arg_date , arg_days ) |


date_add_sub:
	add_sub arg_datetime arg_integer , arg_unit_integer ) |
	add_sub arg_datetime , INTERVAL CONCAT_WS('.' , arg_second , arg_microsecond ) SECOND_MICROSECOND ) |
	add_sub arg_datetime , INTERVAL CONCAT_WS('.' , CONCAT_WS(':' , arg_minute , arg_second ) , arg_microsecond ) MINUTE_MICROSECOND ) |
	add_sub arg_datetime , INTERVAL CONCAT_WS(':' , arg_minute , arg_second ) MINUTE_SECOND ) |
	add_sub arg_datetime , INTERVAL CONCAT_WS('.' , CONCAT_WS(':' , arg_hour , arg_minute, arg_second ) , arg_microsecond ) HOUR_MICROSECOND ) |
	add_sub arg_datetime , INTERVAL CONCAT_WS(':' ,  arg_hour , arg_minute, arg_second ) HOUR_SECOND ) |
	add_sub arg_datetime , INTERVAL CONCAT_WS(':' , arg_hour , arg_minute ) HOUR_MINUTE ) |
	add_sub arg_datetime , INTERVAL CONCAT_WS(' ' , arg_day , CONCAT_WS(':' , arg_hour , arg_minute, CONCAT_WS('.' , arg_second , arg_microsecond ) DAY_MICROSECOND ) |
	add_sub arg_datetime , INTERVAL CONCAT_WS(' ' , arg_day , CONCAT_WS(':' , arg_hour , arg_minute , arg_second ) DAY_SECOND ) |
	add_sub arg_datetime , INTERVAL CONCAT_WS(' ' , arg_day , CONCAT_WS(':' , arg_hour , arg_minute ) DAY_MINUTE ) |
	add_sub arg_datetime , INTERVAL CONCAT_WS(' ' , arg_day , arg_hours ) DAY_HOUR ) |
	add_sub arg_datetime , INTERVAL CONCAT_WS('-' , arg_year , arg_month ) YEAR_MONTH ) ;

arg_unit_integer:
#	MICROSECOND |
 SECOND | MINUTE | HOUR | DAY | WEEK | MONTH | QUARTER | YEAR ;

arg_unit_noninteger:
	SECOND_MICROSECOND | MINUTE_MICROSECOND | MINUTE_SECOND | HOUR_MICROSECOND | HOUR_SECOND | HOUR_MINUTE | DAY_MICROSECOND | DAY_SECOND | DAY_MINUTE | DAY_HOUR | YEAR_MONTH ;

arg_unit:
#	MICROSECOND |
 SECOND | MINUTE | HOUR | DAY | WEEK | MONTH | QUARTER | YEAR | SECOND_MICROSECOND | MINUTE_MICROSECOND | MINUTE_SECOND | HOUR_MICROSECOND | HOUR_SECOND | HOUR_MINUTE | DAY_MICROSECOND | DAY_SECOND | DAY_MINUTE | DAY_HOUR | YEAR_MONTH ;

arg_unit_timestamp:
#	MICROSECOND |
 SECOND | MINUTE | HOUR | DAY | WEEK | MONTH | QUARTER | YEAR ;

add_sub:
	DATE_ADD( | DATE_SUB( | SUBDATE 

arg_mode:
	0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 ;

arg_cast_type:
	DATE | DATETIME | TIME ;
#	DATETIME( precision ) | TIME | TIME( precision ) ;

precision:
	0 | 3 | 6 ;
	
get_format:
	GET_FORMAT( date_time_datetime , country_code ) ;

date_time_datetime:
	DATE | TIME | DATETIME ;

country_code:
	EUR | USA | JIS | ISO | INTERNAL ;


arg_format:
	CONCAT_WS( format_separator , format_list );

format_list:
	format_item , format_item |
	format_item , format_list ;

format_item:
	'%a' | '%b' | '%c' | '%D' | '%d' | '%e' | '%f' | '%H' | '%h' | '%I' | '%i' | '%j' | '%k' | '%l' | '%M' | '%m' | '%p' | '%r' | '%S' | '%s' | '%T' | '%U' | '%u' | '%V' | '%v' | '%W' | '%w' | '%X' | '%x' | '%Y' | '%y' | '%%' | '%x' ;	

format_separator:
	':' | '-' ;
