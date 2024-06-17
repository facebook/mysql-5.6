#update lineitem
#set l_linestatus = 'X'
#where l_shipdate >= '1992-02-01' and l_shipdate <= '1992-02-29';
update lineitem
set l_linestatus = 'X'
where l_orderkey < 10000;
