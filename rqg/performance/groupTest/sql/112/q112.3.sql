select calflushcache();
select l_returnflag,l_discount,l_linenumber, count(*) from lineitem where l_shipdate between '1994-12-01' and '1996-01-31'
     group by 1,2,3 order by 1,2,3;

