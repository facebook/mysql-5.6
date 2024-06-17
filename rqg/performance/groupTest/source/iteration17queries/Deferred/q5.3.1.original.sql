select revenue_month, 
	decode(district,32768,'     n/a',null,' ','District ' || district) district,
	decode( trunc(max(latest_date),'MM') + interval '1' month - interval '1' day, 
	max(latest_date),null, 'Thru-' || to_char(max(latest_date),'mm-dd-yyyy')) latest_date, 
	sales_items, 
	total_revenue, 
	max(latest_date) -120 max_date,
	decode(sign(decode(district,32768,5000000000,null,5000000000,1600000000)-total_revenue),1,'','Revenue Exceeds Threshold') Trend_Alert
from (
select  to_char(l_shipdate,'YYYY-MM') Revenue_Month, 
	l_district district,
	max(l_shipdate) Latest_date,  
	sum(l_extendedprice) Total_Revenue, count(*) Sales_items
from v_load_lines
where l_shipdate >= trunc(to_date('&max_date'),'MM')
group by to_char(l_shipdate,'YYYY-MM'), l_district
ORDER BY 1,2)
group by revenue_month, district, total_revenue, sales_items
order by 1,2;

