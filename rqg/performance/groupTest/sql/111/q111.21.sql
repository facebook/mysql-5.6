select l_linestatus, l_returnflag, count(*) from lineitem where l_linestatus <> 'O' group by l_linestatus, l_returnflag;
