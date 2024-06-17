select p_brand, sum(l_quantity) tot_qty,
	avg(l_quantity) avg_qty 
from lineitem, part
where l_shipdate between '1996-04-01' and '1996-04-14'
and l_partkey = p_partkey
and p_size = 5
group by p_brand
order by 1;
Select calgetstats();
Select now();
select p_brand, sum(l_quantity) tot_qty,
	avg(l_quantity) avg_qty 
from lineitem, part
where l_shipdate between '1996-04-01' and '1996-04-14'
and l_partkey = p_partkey
and p_size = 5
group by p_brand
order by 1;
Select calgetstats();
quit
