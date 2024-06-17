Select count(l_quantity), count(p_size) from lineitem, part where l_partkey = p_partkey and l_orderkey < 1000000 and p_partkey  < 1000000;

