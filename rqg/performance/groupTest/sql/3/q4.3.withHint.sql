Select /*! INFINIDB_ORDERED */ o_orderdate, o_custkey from orders, lineitem where l_partkey < 100000 and l_orderkey = o_orderkey order by 1, 2;
