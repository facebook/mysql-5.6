Select o_orderdate, o_custkey from orders where o_custkey < 1000 and o_orderkey not in (select l_orderkey from lineitem where l_partkey < 100000); 
