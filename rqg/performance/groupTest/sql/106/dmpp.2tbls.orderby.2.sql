Select l_shipmode, o_shippriority  from lineitem, orders where o_orderkey = l_orderkey and  l_orderkey < 1000000  order by 1;

