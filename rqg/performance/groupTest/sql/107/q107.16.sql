select o_orderpriority, max(l_shipdate), avg(o_totalprice), count(*) 
from orders, lineitem 
where o_orderdate > '1997-08-01' and o_totalprice < 1365  
	and o_orderkey = l_orderkey 
and l_shipdate >  '1997-08-01' and l_suppkey < 10000000
group by o_orderpriority
order by o_orderpriority;
select calflushcache();
