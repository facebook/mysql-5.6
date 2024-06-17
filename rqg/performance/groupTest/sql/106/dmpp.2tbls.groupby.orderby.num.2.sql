Select l_shipmode, max(l_shipdate), min(l_shipdate), max(p_size) from lineitem, part where l_orderkey < 1000000 and p_partkey = l_orderkey group by l_shipmode order by 1;
