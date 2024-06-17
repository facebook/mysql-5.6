Select l_quantity, count(*) from lineitem where l_orderkey < 1000000 group by l_quantity;
