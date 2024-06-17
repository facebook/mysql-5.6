select c_custkey, o_orderkey from customer right outer join orders on c_custkey = o_custkey where c_custkey < 10000 and c_nationkey = 4 order by 1, 2;
