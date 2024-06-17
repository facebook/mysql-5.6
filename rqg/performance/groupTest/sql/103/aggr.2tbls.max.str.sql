Select max(o_orderstatus), max(l_shipmode) from orders, lineitem where o_orderkey = l_orderkey  and o_orderkey < 1000000 and l_partkey < 1000000;
