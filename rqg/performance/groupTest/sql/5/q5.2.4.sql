select l_shipdate, l_suppkey, l_quantity, l_extendedprice, l_comment 
from lineitem where l_orderkey = 600000 order by 1, 2, 3, 4, 5;

