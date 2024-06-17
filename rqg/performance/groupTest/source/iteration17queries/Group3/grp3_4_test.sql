select c_custkey, o_orderkey from orders right outer join customer on c_custkey = o_custkey where c_custkey < 10000 and c_nationkey = 4 order by 1, 2;
select calgetstats();
select now();
select c_custkey, o_orderkey from orders right outer join customer on c_custkey = o_custkey where c_custkey < 10000 and c_nationkey = 4 order by 1, 2;
select calgetstats();
quit



