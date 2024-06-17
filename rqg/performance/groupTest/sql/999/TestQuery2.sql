select p_mfgr, count(*), avg(ps_availqty), avg(p_retailprice),
avg(ps_supplycost) from part, partsupp where p_size in (1, 2, 3, 4, 5, 6, 7, 8,
9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 50) and
p_retailprice < 1250  and ps_partkey = p_partkey and ps_suppkey <= 7500000
group by p_mfgr order by p_mfgr;
