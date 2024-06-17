select count(ps_suppkey) 'Count 400 Million 4 byte Ints: From 800 Million Rows' from partsupp 	where ps_suppkey <= 5000000;
