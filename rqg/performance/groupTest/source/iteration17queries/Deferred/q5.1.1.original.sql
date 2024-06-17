select p_brand, sum(l_quantity) tot_qty,
	avg(l_quantity) avg_qty, 
	sum(l_extendedprice * (1- l_discount )) tot_price,
	avg(l_extendedprice * (1- l_discount )) avg_price, count(*)
from lineitem, part
where l_shipdate between '1996-04-01' and '1996-04-14'
and l_partkey = p_partkey
and p_size = 5
group by rollup( p_brand)
order by 1;

