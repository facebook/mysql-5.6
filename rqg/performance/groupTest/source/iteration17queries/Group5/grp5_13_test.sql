select max(l_orderkey), max(l_partkey), max(l_suppkey), count(*) from lineitem
where l_partkey < 25000000
and l_suppkey < 1250000
and l_orderkey < 100000000
and l_linenumber = 4
and l_quantity <= 5;
select calgetstats();
select now();
select max(l_orderkey), max(l_partkey), max(l_suppkey), count(*) from lineitem
where l_partkey < 25000000
and l_suppkey < 1250000
and l_orderkey < 100000000
and l_linenumber = 4
and l_quantity <= 5;
select calgetstats();
quit



