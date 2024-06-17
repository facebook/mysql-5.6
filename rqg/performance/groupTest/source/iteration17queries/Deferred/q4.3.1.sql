Select o_orderdate, o_custkey from orders where o_orderkey in (select l_orderkey from lineitem where l_partkey < 100000); 
