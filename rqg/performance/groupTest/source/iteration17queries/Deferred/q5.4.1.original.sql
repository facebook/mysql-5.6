select to_char(l_shipdate,'yyyy-mm'), sum(l_extendedprice), avg(p_retailprice) 
from lineitem, part
where l_shipdate between '1993-01-01' and '1994-06-30' 
and l_partkey = p_partkey
and p_retailprice >= 2095
and p_size <= 5 
group by rollup( to_char(l_shipdate,'yyyy-mm'))
order by 1,2;

