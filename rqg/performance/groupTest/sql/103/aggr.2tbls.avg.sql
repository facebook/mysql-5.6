Select avg(o_totalprice), avg(l_extendedprice) from orders, lineitem where o_orderkey = l_orderkey  and o_orderkey < 1000000 and l_partkey < 1000000;
