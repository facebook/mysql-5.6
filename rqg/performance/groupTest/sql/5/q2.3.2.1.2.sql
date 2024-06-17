select count(*) from part, lineitem
	where p_retailprice < 944.23 
	and  p_partkey = l_suppkey 
	and l_shipdate < '1992-04-09';
