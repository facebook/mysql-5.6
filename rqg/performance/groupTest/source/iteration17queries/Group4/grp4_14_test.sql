Select l_shipdate Revenue_day, l_discount district, max(l_shipdate) Latest_date, sum(l_extendedprice) Total_Revenue, count(*) Sales_Items from lineitem group by l_shipdate, l_discount order by 1,2;
select calgetstats();
select now();
Select l_shipdate Revenue_day, l_discount district, max(l_shipdate) Latest_date, sum(l_extendedprice) Total_Revenue, count(*) Sales_Items from lineitem group by l_shipdate, l_discount order by 1,2;
select calgetstats();
quit



