Select l_receiptdate - l_shipdate from lineitem where l_orderkey < 1000000 and l_commitdate < l_receiptdate;
