select  n_name, sum(l_quantity), sum(l_extendedprice),
	max(c26_nbr_10),
	sum(c23_nbr_10), 
	avg(c28_nbr_10), 	
	min(c38_nbr_14), 
	max(c61_nbr_20),
	count(c76_nbr_4),
	avg(c89_nbr_7)
from nation, demographics200
where c76_nbr_4 = 5 and c38_nbr_14 < 170000
	and n_nationkey = c83_nbr_6 
group by n_name order by n_name;
