Select orders.* from lineitem, orders where o_custkey < 100000 and l_partkey < 10000 and l_orderkey = o_orderkey;
