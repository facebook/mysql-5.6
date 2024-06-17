Select o_orderpriority, min(o_orderstatus), max(o_orderstatus) from orders where o_orderkey < 1000000 group by o_orderpriority order by o_orderpriority;
