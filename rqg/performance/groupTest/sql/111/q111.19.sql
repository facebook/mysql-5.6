select  count(l_suppkey) 'Count 21 Billion 4 byte Ints: From ~42 Billion Rows' from lineitem where l_suppkey > 5000000;
