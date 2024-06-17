select l_shipdate, sum(l_extendedprice), avg(p_retailprice) 
from part, lineitem
where l_shipdate between '1993-01-01' and '1994-06-30' 
and p_retailprice >= 2095
and p_size <= 5 
and p_partkey = l_partkey
group by l_shipdate
order by 1;
Select calgetstats();
Select now();
select l_shipdate, sum(l_extendedprice), avg(p_retailprice) 
from part, lineitem
where l_shipdate between '1993-01-01' and '1994-06-30' 
and p_retailprice >= 2095
and p_size <= 5 
and p_partkey = l_partkey
group by l_shipdate
order by 1;
Select calgetstats();
quit
