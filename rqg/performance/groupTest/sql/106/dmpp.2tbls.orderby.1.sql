Select l_quantity, p_size from lineitem, part where p_partkey = l_partkey and l_orderkey < 1000000 order by l_quantity; 

