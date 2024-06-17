Select o_orderdate, o_custkey from orders where o_custkey < 1000 and exists (select * from lineitem where l_partkey < 100000 and l_orderkey = o_orderkey); 
