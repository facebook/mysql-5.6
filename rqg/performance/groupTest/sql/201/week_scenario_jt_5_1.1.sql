select l_discount, count(*) from lineitem where l_shipdate between '1992-01-01' and '1996-06-30'
     group by 1 order by 1;

