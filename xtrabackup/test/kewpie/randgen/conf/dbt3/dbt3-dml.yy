#
# This grammar performs DML operations against the DBT-3 dataset. It attempts to use
# realistic values and WHERE conditions by using literals that are appropriate for the particular
# column that is being used .
#

query_init:
	START TRANSACTION ; AUTOCOMMIT OFF ; SET AUTOCOMMIT = OFF ;

query:
	transaction_body ; commit_rollback ;

_table:
	supplier | part | partsupp | customer | orders | lineitem | nation | region ;

commit_rollback:
	COMMIT | COMMIT | COMMIT | COMMIT | ROLLBACK ;

transaction_body:
	dml ; dml ; dml |
	dml ; transaction_body ;

dml:
	select | select | select | select | select |
	insert | insert | insert | insert | insert |
	insert | insert | insert | insert | insert |
	update | update | update | update | delete ;

select:
	SELECT * FROM supplier WHERE cond_s and_or cond_s |
	SELECT p_partkey , p_name , p_mfgr , p_brand , p_type , p_size , p_container , p_comment FROM part WHERE cond_p and_or cond_p |
	SELECT * FROM partsupp WHERE cond_ps and_or cond_ps |
	SELECT * FROM customer WHERE cond_c and_or cond_c |
	SELECT o_orderkey , o_custkey , o_orderstatus , o_orderpriority , o_clerk , o_shippriority , o_comment FROM orders WHERE cond_o and_or cond_o |
	SELECT l_orderkey , l_partkey , l_suppkey , l_linenumber , l_quantity , l_returnflag ,  l_linestatus , l_shipinstruct , l_shipmode , l_comment FROM lineitem WHERE cond_l and_or cond_l |
	SELECT * FROM nation WHERE cond_n and_or cond_n |
	SELECT * FROM region WHERE cond_r and_or cond_r ;

insert:
	insert_s | insert_p | insert_ps | insert_c | insert_o | insert_l | insert_n | insert_r ;

insert_s:
	INSERT INTO supplier ( s_suppkey, s_name , s_address , s_nationkey , s_phone , s_acctbal , s_comment ) insert_s2 ;

insert_s2:
	SELECT suppkey_item , s_name , s_address , s_nationkey , s_phone , s_acctbal , s_comment FROM supplier WHERE cond_s and_or cond_s ORDER BY s_suppkey |
	VALUES value_list_s ;

value_list_s:
	values_s |
	values_s , value_list_s ;

values_s:
	( suppkey_item , s_name_item , address_item , nationkey_item , phone_item , acctbal_item , comment_item ) ;

insert_p:
	INSERT INTO part ( p_partkey , p_name , p_mfgr , p_brand , p_type , p_size , p_container , p_retailprice , p_comment ) insert_p2 ;

insert_p2:
	SELECT partkey_item , p_name , p_mfgr , p_brand , p_type , p_size , p_container , p_retailprice , p_comment FROM part WHERE cond_p and_or cond_p ORDER BY p_partkey |
	VALUES value_list_p ;

value_list_p:
	values_p |
	values_p , value_list_p ;

values_p:
	( partkey_item , p_name_item , mfgr_item, brand_item , type_item , size_item , container_item , retailprice_item , comment_item ) ;

insert_ps:
	INSERT INTO partsupp ( ps_partkey , ps_suppkey , ps_availqty , ps_supplycost , ps_comment ) insert_ps2 ;

insert_ps2:
	SELECT partkey_item , ps_suppkey , ps_availqty , ps_supplycost , ps_comment FROM partsupp WHERE cond_ps and_or cond_ps ORDER BY ps_partkey , ps_suppkey |
	VALUES value_list_ps ;

value_list_ps:
	values_ps |
	values_ps , value_list_ps ;

values_ps:
	( partkey_item , suppkey_item , availqty_item , supplycost_item , comment_item ) ;

insert_c:
	INSERT INTO customer ( c_custkey , c_name , c_address , c_nationkey , c_phone , c_acctbal , c_mktsegment , c_comment ) insert_c2 ;

insert_c2:
	SELECT custkey_item , c_name , c_address , c_nationkey , c_phone , c_acctbal , c_mktsegment , c_comment FROM customer WHERE cond_c and_or cond_c ORDER BY c_custkey |
	VALUES value_list_c ;

value_list_c:
	values_c |
	values_c , value_list_c ;

values_c:
	( custkey_item , c_name_item , address_item , nationkey_item , phone_item , acctbal_item , mktsegment_item , comment_item ) ;

insert_o:
	INSERT INTO orders ( o_orderkey , o_custkey , o_orderstatus , o_totalprice , o_orderDATE , o_orderpriority , o_clerk , o_shippriority , o_comment ) insert_o2 ;

insert_o2:
	SELECT orderkey_item , o_custkey, o_orderstatus, o_totalprice , o_orderDATE , o_orderpriority , o_clerk , o_shippriority , o_comment FROM orders WHERE cond_o and_or cond_o ORDER BY o_orderkey |
	VALUES value_list_o ;

value_list_o:
	values_o |
	values_o , value_list_o ;

values_o:
	( orderkey_item , custkey_item , orderstatus_item , totalprice_item , date_item , orderpriority_item , clerk_item , shippriority_item , comment_item ) ;

insert_l:
	INSERT INTO lineitem ( l_orderkey , l_partkey , l_suppkey , l_linenumber , l_quantity , l_extendedprice , l_discount , l_tax , l_returnflag , l_linestatus , l_shipDATE , l_commitDATE , l_receiptDATE , l_shipinstruct , l_shipmode , l_comment ) insert_l2 ;

insert_l2:
	SELECT orderkey_item , l_partkey , l_suppkey , l_linenumber , l_quantity , l_extendedprice , l_discount , l_tax , l_returnflag , l_linestatus , l_shipDATE , l_commitDATE , l_receiptDATE , l_shipinstruct , l_shipmode , l_comment FROM lineitem WHERE cond_l and_or cond_l ORDER BY l_orderkey , l_linenumber |
	VALUES value_list_l ;

value_list_l:
	values_l |
	values_l , value_list_l ;

values_l:
	( orderkey_item , partkey_item , suppkey_item , linenumber_item , quantity_item , extendedprice_item , discount_item , tax_item , returnflag_item , linestatus_item , date_item , date_item , date_item , shipinstruct_item , shipmode_item , comment_item ) ;

insert_n:
	INSERT INTO nation ( n_nationkey , n_name , n_regionkey , n_comment ) insert_n2 ;

insert_n2:
	SELECT nationkey_item , n_name , n_regionkey , n_comment FROM  nation WHERE cond_n and_or cond_n ORDER BY n_nationkey |
	VALUES values_list_n ;

values_list_n:
	values_n | 
	values_n , values_list_n ;

values_n:
	( nationkey_item , n_name_item , regionkey_item , comment_item ) ;

insert_r:
	INSERT INTO region ( r_regionkey , r_name , r_comment ) insert_r2 ;

insert_r2:
	SELECT regionkey_item , r_name , r_comment FROM region WHERE cond_r and_or cond_r ORDER BY r_regionkey |
	VALUES value_list_r ;

value_list_r:
	values_r |
	values_r , value_list_r ;

values_r:
	( regionkey_item , r_name_item , comment_item ) ;

update:
	UPDATE supplier SET set_s_list WHERE cond_s and_or cond_s |
	UPDATE part SET set_p_list WHERE cond_p and_or cond_p |
	UPDATE partsupp SET set_ps_list WHERE cond_ps and_or cond_ps |
	UPDATE customer SET set_c_list WHERE cond_c and_or cond_c |
	UPDATE orders SET set_o_list WHERE cond_o and_or cond_o |
	UPDATE lineitem SET set_l_list WHERE cond_l and_or cond_l |
	UPDATE nation SET set_n_list WHERE cond_n and_or cond_n |
	UPDATE region SET set_r_list WHERE cond_r and_or cond_r
;

set_s_list:
	set_s | set_s , set_s_list ;

set_p_list:
	set_p | set_p , set_p_list ;

set_ps_list:
	set_ps | set_ps , set_ps_list ;

set_c_list:
	set_c | set_c , set_c_list ;

set_o_list:
	set_o | set_o , set_o_list ;

set_l_list:
	set_l | set_l , set_l_list ;

set_n_list:
	set_n | set_n , set_n_list ;

set_r_list:
	set_r | set_r , set_r_list ;

set_s:
#	s_suppkey = suppkey_item |
	s_name = s_name_item |
	s_address = address_item |
	s_phone	= phone_item |
#	s_acctbal = acctbal_item |
	s_comment = comment_item |
	s_nationkey = nationkey_item ;

set_p:
#	p_partkey = partkey_item |
	p_name = p_name_item |
	p_mfgr = mfgr_item |
	p_brand = brand_item |
	p_type = type_item |
	p_size = size_item |
	p_container = container_item |
#	p_retailprice = retailprice_item |
	p_comment = comment_item ;

set_ps:
#	ps_partkey = partkey_item |
	ps_suppkey = suppkey_item |
#	ps_supplycost = supplcost_item
	ps_comment = comment_item ;

set_c:
#	c_custkey = custkey_item |
	c_name = c_name_item |
	c_address = address_item |
	c_nationkey = nationkey_item |
	c_phone = phone_item |
#	c_acctbal = acctbal_item |
	c_mktsegment = mktsegment_item |
	c_comment = comment_item ;

set_o:
#	o_orderkey = orderkey_item |
	o_custkey  = custkey_item |
	o_orderstatus = orderstatus_item |
#	o_totalprice = totalprice_item |
	o_orderDATE = date_item |
	o_orderpriority = orderpriority_item |
	o_clerk = clerk_item |
	o_shippriority = shippriority_item |
	o_comment = comment_item ;
	
set_l:
#	l_orderkey = orderkey_item |
	l_partkey = partkey_item |
	l_suppkey = suppkey_item |
	l_linenumber = linenumber_item |
	l_quantity = quantity_item |
#	l_extendedprice = extendedprice_item |
#	l_discount = discount_item |
#	l_tax = tax_item |
	l_returnflag = returnflag_item |
	l_linestatus = linestatus_item |
	l_shipDATE = date_item |
	l_commitDATE = date_item |
	l_receiptDATE = date_item |
	l_shipinstruct = shipinstruct_item |
	l_shipmode = shipmode_item |
	l_comment = comment_item ;

set_n:
#	n_nationkey = nationkey_item |
	n_name = n_name_item |
	n_regionkey = regionkey_item |
	n_comment = comment_item ;

set_r:
#	r_regionkey = regionkey_item |
	r_name = r_name_item |
	r_comment = comment_item ;

delete:
	DELETE FROM supplier WHERE cond_s and_or cond_s |
	DELETE FROM part WHERE cond_p and_or cond_p |
	DELETE FROM partsupp WHERE cond_ps and_or cond_ps |
	DELETE FROM customer WHERE cond_c and_or cond_c |
	DELETE FROM orders WHERE cond_o and_or cond_o |
	DELETE FROM lineitem WHERE cond_l and_or cond_l |
	DELETE FROM nation WHERE cond_n and_or cond_n |
	DELETE FROM region WHERE cond_r and_or cond_r ;


field_p:
	p_partkey;

field_s:
	s_suppkey | s_nationkey ;

field_ps:
	ps_partkey | ps_suppkey ;

field_l:
	l_orderkey | l_partkey | l_suppkey | l_linenumber | l_shipDATE | l_commitDATE | l_receiptDATE ;

field_o:
	o_orderkey | o_custkey ;

field_c:
	c_custkey | c_nationkey ;

field_n:
	n_nationkey ;

field_r:
	r_regionkey ;

and_or:
	AND | AND | OR ;

#
# Multi-table WHERE conditions
#

cond_l_o:
	l_extendedprice comp_op o_totalprice | lineitem_date_field comp_op o_orderdate ;

cond_ps_l:
	ps_availqty comp_op l_quantity | ps_supplycost comp_op l_extendedprice ;

cond_s_c:
	c_nationkey comp_op s_nationkey ;
	
#
# Per-table WHERE conditions
#

cond_p:
	p_partkey partkey_clause |
	p_retailprice currency_clause ;
#	p_comment comment_clause ;

cond_s:
	s_suppkey suppkey_clause |
	s_nationkey nationkey_clause |
	s_acctbal currency_clause ;
# |
#	s_comment comment_clause ;

cond_ps:
	ps_partkey partkey_clause |
	ps_suppkey suppkey_clause |
	ps_supplycost currency_clause ;
#	ps_comment comment_clause ;

cond_l:
	l_linenumber linenumber_clause |
	l_shipDATE shipdate_clause |
	l_partkey partkey_clause |
	l_suppkey suppkey_clause |
	l_receiptDATE receiptdate_clause |
	l_orderkey orderkey_clause |
	l_quantity quantity_clause |
	l_commitDATE commitdate_clause |
	l_extendedprice currency_clause ;
#	l_comment comment_clause ;

cond_o:
	o_orderkey orderkey_clause |
	o_custkey custkey_clause |
	o_totalprice currency_clause ;
#	o_comment comment_clause ;

cond_c:
	c_custkey custkey_clause |
	c_acctbal currency_clause ;
#	c_comment comment_clause ;

cond_n:
	n_nationkey nationkey_clause ;
#	n_comment comment_clause ;

cond_r:
	r_regionkey regionkey_clause ;
#	r_comment comment_clause ;

#
# Per-column WHERE conditions
#

comp_op:
        = | = | = | = | = | > | >= | < | <= | <> ;

not:
	| | | | | | | | | NOT ;

shipdate_clause:
	comp_op any_date |
#	not IN ( date_list ) |
	date_between ;

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
	not BETWEEN date_item AND date_item |
	between_two_dates_in_a_year |
	between_two_dates_in_a_month |
	within_a_month ;

day_month_year:
	DAY | MONTH | YEAR ;

any_date:
	{ sprintf("'%04d-%02d-%02d'", $prng->uint16(1992,1998), $prng->uint16(1,12), $prng->uint16(1,27)) } ;

between_two_dates_in_a_year:
	{ my $year = $prng->uint16(1992,1998); return sprintf("BETWEEN '%04d-%02d-%02d' AND '%04d-%02d-%02d'", $year, $prng->uint16(1,12), $prng->uint16(1,27), $year, $prng->uint16(1,12), $prng->uint16(1,27)) } ;

between_two_dates_in_a_month:
	{ my $year = $prng->uint16(1992,1998); my $month = $prng->uint16(1,12); return sprintf("BETWEEN '%04d-%02d-%02d' AND '%04d-%02d-%02d'", $year, $month, $prng->uint16(1,27), $year, $month, $prng->uint16(1,27)) } ;

within_a_month:
	{ my $year = $prng->uint16(1992,1998); my $month = $prng->uint16(1,12); return sprintf("BETWEEN '%04d-%02d-01' AND '%04d-%02d-27'", $year, $month, $year, $month) } ;

# LINENUMBER

linenumber_clause:
	comp_op linenumber_item |
#	not IN ( linenumber_list ) |
	not BETWEEN linenumber_item AND linenumber_item + linenumber_range ;

linenumber_list:
	linenumber_item , linenumber_item |
	linenumber_item , linenumber_list ;

linenumber_item:
	_digit; 

linenumber_range:
	_digit ;

# PARTKEY

partkey_clause:
	comp_op partkey_item |
#	not IN ( partkey_list ) |
	not BETWEEN partkey_item AND partkey_item + partkey_range ;

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
	comp_op suppkey_item |
#	not IN ( suppkey_list ) |
	not BETWEEN suppkey_item AND suppkey_item + _digit ;

suppkey_item:
	_digit | 10 ;

suppkey_list:
	suppkey_item , suppkey_item |
	suppkey_item , suppkey_list ;

# RECEPITDATE

receiptdate_clause:
	comp_op any_date |
#	not IN ( date_list ) |
	date_between ;

# COMMITDATE

commitdate_clause:
	comp_op any_date |
#	not IN ( date_list ) |
	date_between ;

# ORDERKEY

orderkey_clause:
	comp_op orderkey_item |
#	not IN ( orderkey_list ) |
	BETWEEN orderkey_item AND orderkey_item + orderkey_range ;

orderkey_item:
	_tinyint_unsigned | { $prng->uint16(1,1500) } ;

orderkey_list:
	orderkey_item , orderkey_item |
	orderkey_item , orderkey_list ;

orderkey_range:
	_digit | _tinyint_unsigned ;

# QUANTITY

quantity_clause:
	comp_op quantity_item |
#	not IN ( quantity_list ) |
	BETWEEN quantity_item AND quantity_item + quantity_range ;

quantity_list:
	quantity_item , quantity_item |
	quantity_item , quantity_list ;

quantity_item:
	_digit  | { $prng->uint16(1,50) } ;

quantity_range:
	_digit ;

# CUSTKEY

custkey_clause:
	comp_op custkey_item |
#	not IN ( custkey_list ) |
	BETWEEN custkey_item AND custkey_item + custkey_range ;

custkey_item:
	_tinyint_unsigned | { $prng->uint16(1,150) } ;

custkey_list:
	custkey_item , custkey_item |
	custkey_item , custkey_list ;

custkey_range:
	_digit | _tinyint_unsigned ;

# NATIONKEY 

nationkey_clause:
	comp_op nationkey_item |
#	not IN ( nationkey_list ) |
	BETWEEN nationkey_item AND nationkey_item + nationkey_range ;

nationkey_item:
	_digit | { $prng->uint16(0,24) } ;

nationkey_list:
	nationkey_item , nationkey_item |
	nationkey_item , nationkey_list ;

nationkey_range:
	_digit | _tinyint_unsigned ;

# REGIONKEY 

regionkey_clause:
	comp_op regionkey_item |
#	not IN ( regionkey_list ) |
	BETWEEN regionkey_item AND regionkey_item + regionkey_range ;

regionkey_item:
	1 | 2 | 3 | 4 ;

regionkey_list:
	regionkey_item , regionkey_item |
	regionkey_item , regionkey_list ;

regionkey_range:
	1 | 2 | 3 | 4 ;

# COMMENT

comment_clause:
	IS NOT NULL | IS NOT NULL | IS NOT NULL |
	comp_op _varchar(1) |
#	comment_not LIKE CONCAT( comment_count , '%' ) |
	BETWEEN _varchar(1) AND _varchar(1) ;

comment_not:
	NOT | NOT | NOT | ;

comment_count:
	_varchar(1) | _varchar(1) |  _varchar(1) | _varchar(1) | _varchar(2) ;

# CURRENCIES

currency_clause:
	comp_op currency_item |
	BETWEEN currency_item AND currency_item + currency_range ;

currency_item:
	_digit | _tinyint_unsigned | _tinyint_unsigned | _tinyint_unsigned | _mediumint_unsigned ;

currency_range:
	_tinyint_unsigned ;

#
# ITEMS
#

# Supplier Items

s_name_item:
	{ "'Supplier".chr(35).'00000000'.$prng->int(1,5)."'" };

# Part Items

p_name_item:
	'words words';

mfgr_item:
	'Manufacturer1' | 'Manufacturer2' | 'Manufacturer3' | 'Manufacturer4' | 'Manufacturer5' ;

brand_item:
	{"'Brand".$prng->int(1,5).$prng->int(1,5)."'" } ;

type_item:
	'types1 types2 types3';

size_item:
	{ $prng->int(1,50) } ;

container_item:
	'container1 container2';

retailprice_item:
	_tinyint_unsigned ;

# Partsupp Items

availqty_item:
	{ $prng->int(1,9999) } ;

supplycost_item:
	_tinyint_unsigned ;

# Customer Items

c_name_item:
	{ "'Customer".chr(35).'00000000'.$prng->int(1,9)."'" } ;

mktsegment_item:
	segments ;

# Orders Items:

orderstatus_item:
	'F' | 'O' | 'P' ;

totalprice_item:
	_tinyint_unsigned ;

orderpriority_item:
	priorities ;

clerk_item:
	{ "'Clerk".chr(35).'000000'.$prng->int(100,999)."'" } ;

shippriority_item:
	0 ;

# Lineitem Items:

linenumber_item:
	_digit ;

quantity_item:
	{ $prng->int(1,50) } ;

extendedprice_item:
	_tinyint_unsigned ;

discount_item:
	0.0 | 0.01 | 0.02 | 0.03 | 0.04 | 0.05 | 0.06 | 0.07 | 0.08 | 0.09 | 0.10 ;

tax_item:
	0.0 | 0.01 | 0.02 | 0.03 | 0.04 | 0.05 | 0.06 | 0.07 | 0.08 ;

returnflag_item:
	'R' | 'A' | 'N' ;

linestatus_item:
	'O' | 'F' ;

shipinstruct_item:
	'DELIVER IN PERSON' | 'COLLECT COD' | 'NONE' | 'TAKE BACK RETURN' ;

shipmode_item:
	'REG AIR' | 'AIR' | 'RAIL' | 'SHIP' | 'TRUCK' | 'MAIL' | 'FOB' ;

# Nation Items

n_name_item:
	nations ;

# Region Items

r_name_item:
	regions ;

comment_item:
	'sentence' ;

address_item:
	'words words';

phone_item:
	{ "'".$prng->int(1,10).'-'.$prng->int(100,999).'-'.$prng->int(100,999).'-'.$prng->int(1000,9999)."'" } ;

acctbal_item:
	{ $prng->int(-999,9999) } ;

sentence:
	words |
	words words ;

container1:
	SM | LG | MED | JUMBO | WRAP ;

container2:
	CASE | BOX | BAG | JAR | PKG | PACK | CAN | DRUM ;

words:
	nouns | verbs | adjectives | adverbs | prepositions | auxiliaries ;
	
nouns:
	foxes | ideas | theodolites | pinto | beans |
	instructions | dependencies | excuses | platelets |
	asymptotes | courts | dolphins | multipliers |
	sauternes | warthogs | frets | dinos |
	attainments | somas | Tiresias | patterns |
	forges | braids | hockey | players | frays |
	warhorses | dugouts | notornis | epitaphs |
	pearls | tithes | waters | orbits |
	gifts | sheaves | depths | sentiments |
	decoys | realms | pains | grouches |
	escapades ;

verbs:
	sleep | wake | are | cajole |
	haggle | nag | use | boost |
	affix | detect | integrate | maintain |
	nod | was | lose | sublate |
	solve | thrash | promise | engage |
	hinder | print | x-ray | breach |
	eat | grow | impress | mold |
	poach | serve | run | dazzle |
	snooze | doze | unwind | kindle |
	play | hang | believe | doubt ;

adjectives:
	furious | sly | careful | blithe |
	quick | fluffy | slow | quiet |
	ruthless | thin | close | dogged |
	daring | brave | stealthy | permanent |
	enticing | idle | busy | regular |
	final | ironic | even | bold |
	silent ;

adverbs:
	sometimes | always | never | furiously |
	slyly | carefully | blithely | quickly |
	fluffily | slowly | quietly | ruthlessly |
	thinly | closely | doggedly | daringly |
	bravely | stealthily | permanently | enticingly |
	idly | busily | regularly | finally |
	ironically | evenly | boldly | silently ;

prepositions:
	about | above | according to | across |
	after | against | along | alongside of |
	among | around | at | atop |
	before | behind | beneath | beside |
	besides | between | beyond | by |
	despite | during | except | for |
	from | in place of | inside | instead of |
	into | near | of | on |
	outside | over | past | since |
	through | throughout | to | toward |
	under | until | up | upon |
	without | with | within ;

auxiliaries:
	do | may | might | shall |
	will | would | can | could |
	should | ought to | must | will have to |
	shall | have to | could have to | should have to | must have to |
	need to | try to ;

names:
	almond | antique | aquamarine | azure | beige | bisque | black | blanched | blue | blush |
	brown | burlywood | burnished | chartreuse | chiffon | chocolate | coral | cornflower |
	cornsilk | cream | cyan | dark | deep | dim | dodger | drab | firebrick | floral | forest |
	frosted | gainsboro | ghost | goldenrod | green | grey | honeydew | hot | indian | ivory |
	khaki | lace | lavender | lawn | lemon | light | lime | linen | magenta | maroon | medium |
	metallic | midnight | mint | misty | moccasin | navajo | navy | olive | orange | orchid | pale |
	papaya | peach | peru | pink | plum | powder | puff | purple | red | rose | rosy | royal |
	saddle | salmon | sandy | seashell | sienna | sky | slate | smoke | snow | spring | steel | tan |
	thistle | tomato | turquoise | violet | wheat | white | yellow ;

segments:
	'AUTOMOBILE' | 'BUILDING' | 'FURNITURE' | 'MACHINERY' | 'HOUSEHOLD' ;

priorities:
	'1-URGENT' | '2-HIGH' | '3-MEDIUM' | '4-NOT SPECIFIED' | '5-LOW' ;

nations:
	'ALGERIA' | 'ARGENTINA' | 'BRAZIL' |
	'CANADA' | 'EGYPT' | 'ETHIOPIA' |
	'FRANCE' | 'GERMANY' | 'INDIA' |
	'INDONESIA' | 'IRAN' | 'IRAQ' | 
	'JAPAN' | 'JORDAN' | 'KENYA' |
	'MOROCCO' | 'MOZAMBIQUE' |  'PERU' |
	'CHINA' | 'ROMANIA' | 'SAUDI ARABIA' | 
	'VIETNAM' | 'RUSSIA' | 'UNITED KINGDOM' |
	'UNITED STATES' ;

regions:
	'AFRICA' | 'AMERICA' | 'ASIA' | 'EUROPE' | 'MIDDLE EAST' ;

types1:
	STANDARD | SMALL | MEDIUM | LARGE | ECONOMY | PROMO ;

types2:
	ANODIZED | BURNISHED | PLATED | POLISHED | BRUSHED ;

types3:
	TIN | NICKEL | BRASS | STEEL | COPPER ;
