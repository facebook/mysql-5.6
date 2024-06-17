select  n_name, l_commitdate, sum(s_acctbal) sum_bal, 
sum(l_extendedprice) sum_price, min(s_suppkey) minskey, count(*)
from nation, supplier, lineitem
where s_nationkey in (1,2)
and l_commitdate between '1998-01-01' and '1998-01-07'
and n_nationkey = s_nationkey
and s_suppkey = l_suppkey
group by n_name, l_commitdate
order by 1, 2;
