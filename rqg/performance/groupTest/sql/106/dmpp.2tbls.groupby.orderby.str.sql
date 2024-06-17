select o_orderpriority, min(o_orderstatus), max(o_orderstatus), count(l_orderkey) 
from orders, lineitem
where o_orderkey < 1000000
and o_orderkey = l_orderkey 
group by o_orderpriority
order by o_orderpriority;

