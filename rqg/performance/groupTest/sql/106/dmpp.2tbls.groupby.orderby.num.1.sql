Select l_quantity, max(p_retailprice) from lineitem, part where l_orderkey < 1000000 and p_partkey = l_orderkey group by l_quantity order by l_quantity;

