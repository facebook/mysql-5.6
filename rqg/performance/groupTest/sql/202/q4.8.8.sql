Select o_shippriority, sum(o_totalprice), avg(o_totalprice), count(*) from orders where o_orderkey < 1000000 group by o_shippriority;
