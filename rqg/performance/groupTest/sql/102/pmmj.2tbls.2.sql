select p_partkey from part, lineitem 
where p_partkey = l_partkey and 
p_partkey between 0 and 502000 and p_size between 0 and 1 and 
l_partkey between 450000 and 200000000 and l_shipdate between '1992-01-01' and '1992-04-09';
