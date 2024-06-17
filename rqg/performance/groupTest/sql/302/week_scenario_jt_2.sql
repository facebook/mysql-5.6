select '1992-01-01' + interval rand() * 365 day into @date_var;
select l_shipdate, l_returnflag Returnflag, l_linestatus Status,
        avg(l_extendedprice) avgprice, count(*),
        min(l_extendedprice) minprice, max(l_extendedprice) maxprice
from lineitem
where l_shipdate = @date_var
group by 1,2,3;

