query:
	SELECT distinct select_item FROM lineitem index_hint WHERE where order_by |
	SELECT aggregate key_field ) FROM lineitem WHERE where ;

order_by:
        | | ORDER BY key_field , l_orderkey , l_linenumber ;

distinct:
	| | | | DISTINCT ;

where:
	where_list and_or where_list |
	( where_list ) and_or ( where_list ) ;

where_list:
	( where_item ) and_or ( where_list ) |
	( where_item ) and_or ( where_item ) |
	where_item and_or where_list |
	where_item and_or where_item |
	where_item and_or where_item and_or where_item |
	where_item ;

where_item:
	key_clause |
	key_clause or_and key_clause ;

key_clause:
	linenumber_clause |
	shipdate_clause |
	partkey_clause |
	suppkey_clause |
	receiptdate_clause |
	orderkey_clause |
	quantity_clause |
	commitdate_clause ;

aggregate:
	MIN( | MAX( | COUNT( | COUNT( ;

and_or:
	AND | AND | AND | AND | OR ;

or_and:
        OR | OR | OR | OR | AND ;

comp_op:
        = | = | = | = | = | = |
        != | > | >= | < | <= | <> ;

not:
	| | | | | | | | | NOT ;

shipdate_clause:
	l_shipdate comp_op any_date |
	l_shipdate not IN ( date_list ) |
	l_shipdate date_between ;

date_list:
	date_item , date_item |
	date_list , date_item ;

date_item:
	any_date | any_date | any_date | any_date | any_date |
	any_date | any_date | any_date | any_date | any_date |
	any_date | any_date | any_date | any_date | any_date |
	any_date | any_date | any_date | any_date | any_date |
	'1992-01-08' | '1998-11-27' ;

date_between:
	BETWEEN date_item AND date_item |
	between_two_dates_in_a_year |
	between_two_dates_in_a_month |
	within_a_month ;

day_month_year:
	DAY | MONTH | YEAR ;

any_date:
	{ sprintf("'%04d-%02d-%02d'", $prng->uint16(1992,1998), $prng->uint16(1,12), $prng->uint16(1,28)) } ;

between_two_dates_in_a_year:
	{ my $year = $prng->uint16(1992,1998); return sprintf("BETWEEN '%04d-%02d-%02d' AND '%04d-%02d-%02d'", $year, $prng->uint16(1,12), $prng->uint16(1,28), $year, $prng->uint16(1,12), $prng->uint16(1,28)) } ;

between_two_dates_in_a_month:
	{ my $year = $prng->uint16(1992,1998); my $month = $prng->uint16(1,12); return sprintf("BETWEEN '%04d-%02d-%02d' AND '%04d-%02d-%02d'", $year, $month, $prng->uint16(1,28), $year, $month, $prng->uint16(1,28)) } ;

within_a_month:
	{ my $year = $prng->uint16(1992,1998); my $month = $prng->uint16(1,12); return sprintf("BETWEEN '%04d-%02d-01' AND '%04d-%02d-29'", $year, $month, $year, $month) } ;

# LINENUMBER

linenumber_clause:
	l_linenumber comp_op linenumber_item |
	l_linenumber not IN ( linenumber_list ) |
	l_linenumber BETWEEN linenumber_item AND linenumber_item + linenumber_range ;

linenumber_list:
	linenumber_item , linenumber_item |
	linenumber_item , linenumber_list ;

linenumber_item:
	_digit; 

linenumber_range:
	_digit ;

# PARTKEY

partkey_clause:
	l_partkey comp_op partkey_item |
	l_partkey not IN ( partkey_list ) |
	l_partkey BETWEEN partkey_item AND partkey_item + partkey_range ;

partkey_list:
	partkey_item , partkey_item |
	partkey_item , partkey_list ;

partkey_range:
	_digit | _tinyint_unsigned;

partkey_item:
	_tinyint_unsigned  | _tinyint_unsigned | _tinyint_unsigned | _tinyint_unsigned | _tinyint_unsigned |
	_tinyint_unsigned  | _tinyint_unsigned | _tinyint_unsigned | _tinyint_unsigned | _tinyint_unsigned |
	_tinyint_unsigned  | _tinyint_unsigned | _tinyint_unsigned | _tinyint_unsigned | _tinyint_unsigned |
	_tinyint_unsigned  | _tinyint_unsigned | _tinyint_unsigned | _tinyint_unsigned | _tinyint_unsigned |
	_digit | 200 | 0 ;

# SUPPKEY

suppkey_clause:
	l_suppkey comp_op suppkey_item |
	l_suppkey not IN ( suppkey_list ) |
	l_suppkey BETWEEN suppkey_item AND suppkey_item + _digit ;

suppkey_item:
	_digit | 10 ;

suppkey_list:
	suppkey_item , suppkey_item |
	suppkey_item , suppkey_list ;

# RECEPITDATE

receiptdate_clause:
	l_receiptDATE comp_op any_date |
	l_receiptDATE not IN ( date_list ) |
	l_receiptDATE date_between ;

# COMMITDATE

commitdate_clause:
	l_commitDATE comp_op any_date |
	l_commitDATE not IN ( date_list ) |
	l_commitDATE date_between ;

# ORDERKEY

orderkey_clause:
	l_orderkey comp_op orderkey_item |
	l_orderkey not IN ( orderkey_list ) |
	l_orderkey BETWEEN orderkey_item AND orderkey_item + orderkey_range ;

orderkey_item:
	_tinyint_unsigned | { $prng->uint16(1,1500) } ;

orderkey_list:
	orderkey_item , orderkey_item |
	orderkey_item , orderkey_list ;

orderkey_range:
	_digit | _tinyint_unsigned ;

# QUANTITY

quantity_clause:
	l_quantity comp_op quantity_item |
	l_quantity not IN ( quantity_list ) |
	l_quantity BETWEEN quantity_item AND quantity_item + quantity_range ;

quantity_list:
	quantity_item , quantity_item |
	quantity_item , quantity_list ;

quantity_item:
	_digit  | { $prng->uint16(1,50) } ;

quantity_range:
	_digit ;

index_hint:
	| FORCE KEY ( key_list );

key_list:
	key_name , key_name |
	key_name , key_list ;

select_item:
	* | key_field ;

key_field:
	l_linenumber |
	l_shipdate |
	l_partkey |
	l_suppkey |
	l_receiptdate |
	l_orderkey |
	l_quantity |
	l_commitDATE ;

key_name:
	PRIMARY |
	i_l_shipdate |
	i_l_suppkey_partkey |
	i_l_partkey |
	i_l_suppkey |
	i_l_receiptdate |
	i_l_orderkey |
	i_l_orderkey_quantity |
	i_l_commitdate ;
