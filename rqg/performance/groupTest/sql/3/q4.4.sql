Select o_orderdate, o_custkey from orders,lineitem where o_custkey < 1000 and o_orderkey = l_orderkey order by 1, 2; 
