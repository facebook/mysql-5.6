select p_mfgr, count(*), avg(ps_availqty), avg(p_retailprice), avg(ps_supplycost) 
from part, partsupp 
where p_size = 50 and p_retailprice < 1250 
	and ps_partkey = p_partkey 
and ps_suppkey <= 10000000
group by p_mfgr
order by p_mfgr;

