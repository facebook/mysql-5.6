select o_orderstatus, count(*), sum(o_totalprice), avg(o_totalprice) from orders where o_custkey <= 75000000 group by o_orderstatus order by o_orderstatus;
