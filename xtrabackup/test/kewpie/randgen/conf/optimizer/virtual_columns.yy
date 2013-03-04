#
# At the top of this grammar, we define the data types, the literals and the functions that will
# be used in the test. Some combinations of functions, data types and literals may produce
# false positives when used in a comparison test
#

constant:
	_digit | regular_field ;
	# _letter | _varchar(4) ;

field_type:
	DOUBLE ;
	#| CHAR(255) BINARY | DATETIME ;


nested_expr:
	( comparison_operator ) | ( logical_operator ) | ( logical_operator ) |
	( control_flow_function ) | 
#	( string_function ) |
#	( string_comparison_function ) |
	( arithmetic_function ) |
#	( date_and_time_function ) |
	( mathematical_function ) ;

#
# We use multiple CREATE TABLE so that if a given table definition is invalid
# (such as a constant expression in a virtual column), subsequent CREATEs will
# create a valid table
#


query_init:
	drop_table ; create_table ; create_table ; create_table ;

query:
	drop_table ; create_table ; create_table ; create_table | create_table |
	select_dml | select_dml | select_dml | select_dml | select_dml |
	select_dml | select_dml | select_dml | select_dml | select_dml |
	select_dml | select_dml | select_dml | select_dml | select_dml |
	select_dml | select_dml | select_dml | select_dml | select_dml |
	select_dml | select_dml | select_dml | select_dml | select_dml |
	select_dml | select_dml | select_dml | select_dml | select_dml ;

select_dml:
	select | dml | dml | dml | dml | dml ;
	
drop_table:
	DROP TABLE IF EXISTS X ;

#
# Using the /*executor1 */ and /*executor2 */ syntax, we make sure that, in a comparison test PERSISTENT virtual columns
# are created on the first server while non-persistent ones are created on the second server
# We use the same syntax for KEY and FORCE|IGNORE KEY because only PERSISTENT virtual columns can have keys
#

create_table:
	CREATE TABLE IF NOT EXISTS X (
		f1 field_type null default,
		f2 field_type null default,
		f3 field_type null default,
		f4 field_type null default,
		v1 field_type AS ( virtual_expr ) /*executor1 PERSISTENT */ /*executor2 VIRTUAL */ ,
		v2 field_type AS ( virtual_expr ) /*executor1 PERSISTENT */ /*executor2 VIRTUAL */ ,
		v3 field_type AS ( virtual_expr ) /*executor1 PERSISTENT */ /*executor2 VIRTUAL */ ,
		v4 field_type AS ( virtual_expr ) /*executor1 PERSISTENT */ /*executor2 VIRTUAL */
		/*executor1 , KEY (v3), KEY (v4) */
	);

null:
	| NOT NULL ;

default:
	| DEFAULT '0' ;

select:
	select_plain |
	select_group_by ;

select_plain:
	SELECT any_field AS a1 , any_field AS a2  FROM virtual_table_name force_key WHERE where_condition order_by_limit;

select_group_by:
	SELECT v3 , aggregate_item FROM virtual_table_name force_key WHERE where_condition GROUP BY v3 order_by |
	SELECT v4 , aggregate_item FROM virtual_table_name force_key WHERE where_condition GROUP BY v4 order_by;

force_key:
	| /*executor1 force_ignore KEY (v3, v4) */ ;

force_ignore:
	FORCE | IGNORE ;

select_list:
	select_list, any_field | any_field ;

aggregate_item:
	SUM(any_field) | COUNT(*) | COUNT(any_field) | MIN(any_field) | MAX(any_field) ;

order_by_limit:
	|
	ORDER BY any_field_list |
	ORDER BY any_field_list , complete_field_list LIMIT _digit;

order_by:
	|
	ORDER BY any_field_list ;

any_field_list:
	any_field_list , any_field | any_field ;

dml:
	insert_replace | insert_replace |
	update | delete ;

insert_replace:
	i_s INTO virtual_table_name ( regular_field , regular_field ) VALUES ( constant , constant ) |
	i_s INTO virtual_table_name ( f1 , f2 , f3 , f4 ) VALUES ( constant , constant , constant , constant ) |
	i_s INTO virtual_table_name ( f1 , f2 , f3 , f4 ) SELECT any_field , any_field , any_field , any_field FROM virtual_table_name WHERE where_condition ORDER BY complete_field_list LIMIT _digit ;

i_s:
	INSERT ignore |
	REPLACE ;

update:
	UPDATE virtual_table_name SET regular_field = constant WHERE where_condition |
	UPDATE virtual_table_name SET regular_field = virtual_field WHERE where_condition ;

delete:
	DELETE FROM virtual_table_name WHERE where_condition ORDER BY complete_field_list LIMIT _digit ;

complete_field_list:
	f1,f2,f3,f4,v1,v2,v3,v4;

where_condition:
	where_condition AND any_field op constant |
	where_condition OR any_field op constant |
	any_field op constant | any_field op constant |
	any_field op constant | any_field op constant |
	any_field op constant | any_field op constant |
	any_field op any_field | any_field op any_field |
	any_field not BETWEEN _digit AND _digit ;

not:
	| NOT;

op:
	< | > | = | <> | <=> | != ;

ignore:
	| IGNORE ;

any_field:
	regular_field | virtual_field ;

regular_field:
	f1 | f2 | f3 | f4 ;

virtual_field:
	v1 | v2 | v3 | v4 ;	


virtual_table_name:
	X ;

virtual_expr:
	nested_expr ;

expr:
	regular_field | regular_field | regular_field | nested_expr | nested_expr;

comparison_operator:
        expr = expr |
        expr <=> expr |
        expr <> expr | expr != expr |
        expr <= expr |
        expr < expr |
        expr >= expr |
        expr > expr |
        expr IS not boolean_value |
        expr IS not NULL |
        expr not BETWEEN ( expr ) AND ( expr ) |
        COALESCE( expr_list ) |
        GREATEST( expr , expr_list ) |
        expr not IN ( expr_list ) |
        ISNULL( expr ) |
	INTERVAL( expr , expr_list ) |
        LEAST( expr, expr_list ) ;

logical_operator:
 	NOT ( expr ) |
        ( expr ) AND ( expr ) |
        ( expr ) OR ( expr ) ;
#       ( expr ) XOR ( expr ) ;	# MySQL bug #55365 

string_comparison_function:
        expr not LIKE pattern |
        STRCMP(expr, expr) ;

regexp:
        expr not REGEXP pattern ;

pattern:
        '%' | '.*' ;

control_flow_function:
	CASE expr WHEN expr THEN expr WHEN expr THEN expr ELSE expr END |
	IF( expr , expr , expr ) |
	IFNULL( expr , expr ) |
	NULLIF( expr , expr ) ;

string_function:
	ASCII( expr ) |
	BIN( expr ) |
	BIT_LENGTH( expr ) |
	CHAR( expr ) |
	CHAR( expr USING _charset ) |
	CHAR_LENGTH( expr ) |
	CHAR_LENGTH( str ) |
	CONCAT( expr_list ) |
	CONCAT_WS( expr , expr_list ) |
	ELT( expr , expr_list ) |
	EXPORT_SET( expr , expr , expr , expr , expr ) |
	FIELD( expr , expr_list ) |
	FIND_IN_SET( expr , expr_list ) |
	FORMAT( expr , expr ) |
	HEX( expr ) |
	INSERT( expr , expr , expr , expr ) |
	INSTR( expr , expr ) |
	LCASE( expr ) |
	LEFT( expr , expr ) |
	LENGTH( expr ) |
	LOAD_FILE( expr ) |
	LOCATE( expr , expr ) |
	LOCATE( expr , expr , expr) |
	LOWER( expr ) |
	LPAD( expr , expr , expr ) |
	LTRIM( expr ) |
	MAKE_SET( expr , expr_list ) |
	MID( expr , pos , len) |
	OCT( expr ) |
	OCTET_LENGTH( expr ) |
	ORD( expr ) |
	POSITION( expr IN expr ) |
	QUOTE( expr ) |
	REPEAT( expr , expr ) |
	REPLACE( expr , expr , expr ) |
	REVERSE( expr ) |
	RIGHT( expr , expr ) |
	RPAD( expr , expr , expr ) |
	RTRIM( expr ) |
	SOUNDEX( expr ) |
	( expr SOUNDS LIKE expr ) |
	SPACE( expr ) |
	SUBSTR( expr , expr ) |
	SUBSTR( expr , expr , expr ) |
	SUBSTRING_INDEX( expr , expr , expr ) |
	TRIM( both_leading_trailing expr FROM expr ) |
	UCASE( expr ) |
	UNHEX( expr ) |
	UPPER( expr ) ;

arithmetic_function:
	expr + expr |
	expr - expr |
	( - expr ) |
	expr * expr |
	expr / expr |
	expr DIV expr |
	expr % expr ;

mathematical_function:
	ABS( expr ) |
	ACOS( expr ) |
	ASIN( expr ) |
	ATAN( expr ) |
	ATAN( expr , expr ) |
	CEIL( expr ) |
	CEILING( expr ) |
	CONV( expr , expr , expr ) |
	COS( expr ) |
	COT( expr ) |
	CRC32( expr ) |
	DEGREES( expr ) |
	EXP( expr ) |
	FLOOR( expr ) |
	FORMAT( expr , expr ) |
	HEX( expr ) |
	LN( expr ) |
	LOG( expr ) |
	LOG( expr , expr ) |
	LOG2( expr ) |
	LOG10( expr ) |
	MOD( expr , expr ) |
	PI() |
	POW( expr , expr ) |
	POWER( expr , expr ) |
	RADIANS( expr ) |
	RAND( expr ) |
	ROUND( expr ) |
	ROUND( expr , expr ) |
	SIGN( expr ) |
	SIN( expr ) |
	SQRT( expr ) |
	TAN( expr ) ;
#	TRUNCATE( expr , expr ) ;	# MySQL bug #55365 

date_and_time_function:
	ADDDATE( expr , INTERVAL expr unit) |
	ADDDATE( expr , expr ) |
	ADDTIME( expr , expr ) |
	CONVERT_TZ( expr , tz , tz ) |
	CURDATE() |
	CURTIME() |
	CURRENT_TIMESTAMP() |
	DATE( expr ) |
	DATEDIFF( expr , expr ) |
	DATE_ADD( expr INTERVAL expr unit ) |
	DATE_FORMAT( expr , expr ) |
	DATE_SUB( expr INTERVAL expr unit ) |
	DAY( expr ) |
	DAYNAME( expr ) |
	DAYOFMONTH( expr ) |
	DAYOFWEEK( expr ) |
	DAYOFYEAR( expr ) |
	EXTRACT( unit FROM expr ) |
	FROM_DAYS( expr ) |
	FROM_UNIXTIME( expr ) |
	FROM_UNIXTIME( expr , expr ) |
	GET_FORMAT( date_time_datetime, date_format ) |
	HOUR( expr ) |
	LAST_DAY( expr ) |
	LOCALTIME() |
	MAKEDATE( expr , expr ) |
	MAKETIME( expr , expr , expr ) |
	MICROSECOND( expr ) |
	MINUTE( expr ) |
	MONTH( expr ) |
	MONTHNAME( expr ) |
	NOW() |
	PERIOD_ADD( expr , expr ) |
	PERIOD_DIFF( expr , expr ) |
	QUARTER( expr ) |
	SECOND( expr ) |
	SEC_TO_TIME( expr ) |
	STR_TO_DATE( expr , expr ) |
	SUBDATE( expr INTERVAL expr unit ) |
	SUBDATE( expr , expr ) |
	SUBTIME( expr , expr ) |
	SYSDATE() | 
	TIME( expr ) |
	TIMEDIFF( expr , expr ) |
	TIMESTAMP( expr ) |
	TIMESTAMP( expr , expr ) |
	TIMESTAMPADD( unit , expr , expr ) |
	TIMESTAMPDIFF( unit , expr , expr ) |
	TIME_FORMAT( expr , expr ) |
	TIME_TO_SEC( expr ) |
	TO_DAYS( expr ) |
	UNIX_TIMESTAMP() |
	UNIX_TIMESTAMP( expr ) |
	UTC_DATE() |
	UTC_TIME() |
	UTC_TIMESTAMP() |
	WEEK( expr , expr ) |
	WEEKDAY( expr ) |
	WEEKOFYEAR( expr ) |
	YEAR( expr ) |
	YEARWEEK( expr ) |
	YEARWEEK( expr , expr ) ; 

date_time_datetime:
	DATE | TIME | DATETIME ;

date_format:
	'EUR' | 'USA' | 'JIS' | 'ISO' | 'INTERNAL' ;
	
unit:
	MICROSECOND | SECOND | MINUTE | HOUR | DAY | WEEK | MONTH | QUARTER | YEAR |
	SECOND_MICROSECOND |
	MINUTE_MICROSECOND | MINUTE_SECOND |
	HOUR_MICRSECOND | HOUR_SECOND | HOUR_MINUTE |
	DAY_MICROSECOND | DAY_SECOND | DAY_MINUTE | DAY_HOUR |
	YEAR_MONTH ;

tz:
	SYSTEM ;

both_leading_trailing:
	| BOTH | LEADING | TRAILING ;

boolean_value:
        TRUE | FALSE | UNKNOWN ;

expr_list:
	expr_list , expr | expr , expr | expr | expr | expr ;
