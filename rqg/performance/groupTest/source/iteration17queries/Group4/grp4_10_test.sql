Select l_shipmode, max(l_shipdate), min(l_shipdate) from lineitem where l_orderkey < 1000000 group by l_shipmode order by 1;
select calgetstats();
select now();
Select l_shipmode, max(l_shipdate), min(l_shipdate) from lineitem where l_orderkey < 1000000 group by l_shipmode order by 1;
select calgetstats();
quit



