select count(*),max(c26_nbr_10),max(c23_nbr_10),max(c28_nbr_10),
max(c38_nbr_14),max(c61_nbr_20),max(c76_nbr_4),max(c89_nbr_7)
from demographics200 
where 		l_orderkey < 7000009
		and c23_nbr_10 < 70000
		and c26_nbr_10 < 700000
		and c28_nbr_10 < 7000
		and c38_nbr_14 < 7050000
		and c61_nbr_20 < 70500000
		and c76_nbr_4 < 28 ;
