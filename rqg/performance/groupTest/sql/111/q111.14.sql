select o_orderstatus, count(*), sum(o_totalprice), avg(o_totalprice) from orders where o_orderkey > 3000000000 group by o_orderstatus order by o_orderstatus;
