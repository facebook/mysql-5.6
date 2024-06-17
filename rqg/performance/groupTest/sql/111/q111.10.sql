select count(o_orderkey) 'Count 750M 8 byte BigInts: From 1.5 Billion Rows'   from orders 	where o_orderkey > 3000000000;
