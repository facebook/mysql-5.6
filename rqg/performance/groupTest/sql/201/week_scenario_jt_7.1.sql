select max(l_orderkey), max(l_partkey), max(l_suppkey), count(l_orderkey) 
from lineitem
where l_partkey < 45000000
and l_suppkey < 2250000
and l_orderkey < 200000000
and l_shipdate between '1992-03-01' and '1992-03-31'
and l_linenumber <= 4
and l_quantity <= 25;
