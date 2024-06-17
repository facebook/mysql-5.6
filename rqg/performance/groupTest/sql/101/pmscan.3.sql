select max(l_partkey), count(*) from lineitem where l_partkey <= 20000000;
