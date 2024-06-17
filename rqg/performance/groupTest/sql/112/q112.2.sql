select l_discount, count(*) from lineitem where l_shipdate between '1994-12-01' and '1996-01-31'
     group by 1 order by 1;

