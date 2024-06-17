select count(o_custkey) 'Count 750M 4 byte Ints: From 1.5 Billion Rows'  from orders 	where o_custkey <= 75000000;
