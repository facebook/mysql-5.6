select count(*) from part, lineitem
	where p_retailprice < 904.01 
	and  p_partkey = l_suppkey 
	and l_shipdate < '1993-04-07';
