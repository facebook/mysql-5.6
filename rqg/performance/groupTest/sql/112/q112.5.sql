select calflushcache();
select l_returnflag,l_discount,l_linenumber,l_linestatus,l_tax, count(*) from lineitem where l_shipdate between '1994-12-01' and '1996-01-31'
     group by 1,2,3,4,5 order by 1,2,3,4,5;

