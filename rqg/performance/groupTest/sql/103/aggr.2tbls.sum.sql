Select sum(o_totalprice), sum(l_extendedprice)  from orders, lineitem where o_orderkey < 1000000 and l_partkey < 1000000 and o_orderkey = l_orderkey;
