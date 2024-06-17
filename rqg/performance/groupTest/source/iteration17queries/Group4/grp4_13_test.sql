select 
	l_returnflag,
	l_linestatus,
	sum(l_quantity) as sum_qty,
	sum(l_extendedprice) as sum_base_price,
	avg(l_quantity) as avg_qty,
	avg(l_extendedprice) as avg_price,
	avg(l_discount) as avg_disc,
	count(*) as count_order
from 
	lineitem
where 
	l_shipdate <= date '1998-09-26'

group by l_returnflag,
	l_linestatus
order by 
	l_returnflag,
	l_linestatus;
select calgetstats();
select now();
select 
	l_returnflag,
	l_linestatus,
	sum(l_quantity) as sum_qty,
	sum(l_extendedprice) as sum_base_price,
	avg(l_quantity) as avg_qty,
	avg(l_extendedprice) as avg_price,
	avg(l_discount) as avg_disc,
	count(*) as count_order
from 
	lineitem
where 
	l_shipdate <= date '1998-09-26'

group by l_returnflag,
	l_linestatus
order by 
	l_returnflag,
	l_linestatus;
select calgetstats();
quit



