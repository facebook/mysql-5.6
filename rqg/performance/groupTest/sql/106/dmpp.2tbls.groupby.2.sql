Select  l_shipmode, max(l_shipdate), min(l_shipdate), max(o_orderdate), min(o_orderdate) from lineitem, orders where o_orderkey = l_orderkey and l_orderkey < 1000000 group by l_shipmode;

