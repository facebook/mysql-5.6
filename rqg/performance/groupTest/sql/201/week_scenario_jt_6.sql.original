select 	r1.r_name Sales_Region, n1.n_name Sales_Nation,
	r2.r_name Supplier_Region, n2.n_name Supplier_Nation,
	count(*)
from 	region r1 
	join nation n1 on (r1.r_regionkey = n1.n_regionkey)
	join customer on (c_nationkey = n1.n_nationkey)
	join orders on (c_custkey = o_custkey)
	join lineitem on (l_orderkey = o_orderkey)
	join supplier on l_suppkey = s_suppkey
	join nation n2 on (s_nationkey = n2.n_nationkey)
	join region r2 on (r2.r_regionkey = n2.n_regionkey)
where l_shipdate between '1992-01-02' and  '1992-12-31'
 and o_orderdate between '1992-01-02' and  '1992-12-31'
 and n1.n_nationkey = 4
 and n2.n_nationkey in (5,6,7,8) 
group by 1,2,3,4
order by 1,2,3,4;
