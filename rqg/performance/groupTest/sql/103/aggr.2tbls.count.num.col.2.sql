Select count(l_extendedprice), count(o_totalprice) from lineitem, orders where o_orderkey = l_orderkey and l_orderkey < 1000000 and o_orderkey < 1000000;
