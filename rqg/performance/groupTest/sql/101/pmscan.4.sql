select min(l_orderkey), max(l_partkey), min(l_suppkey), avg(l_linenumber), sum(l_extendedprice), avg(l_discount), count(l_tax), count(l_shipdate) from lineitem where l_shipdate <= '1992-08-31';
