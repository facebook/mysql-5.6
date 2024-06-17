select p_brand, sum(l_quantity) tot_qty,
	avg(l_quantity) avg_qty
from part, lineitem
where l_shipdate between '1996-04-01' and '1996-04-14'
and p_size = 5
and p_partkey = l_partkey
group by p_brand
order by 1;
