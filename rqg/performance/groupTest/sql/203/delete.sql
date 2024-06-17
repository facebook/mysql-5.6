# We used to delete the rows in batches in InfiniDB due to memory issue.
# With MyRocks, we will run a single delete statement instead
#delete from lineitem where l_orderkey <= 500000000 and l_linestatus = 'X';
#delete from lineitem where l_orderkey <= 1000000000 and l_linestatus = 'X';
#delete from lineitem where l_orderkey <= 1500000000 and l_linestatus = 'X';
#delete from lineitem where l_orderkey <= 2000000000 and l_linestatus = 'X';
#delete from lineitem where l_orderkey <= 2500000000 and l_linestatus = 'X';
#delete from lineitem where l_orderkey <= 3000000000 and l_linestatus = 'X';
#delete from lineitem where l_orderkey <= 3500000000 and l_linestatus = 'X';
#delete from lineitem where l_orderkey <= 4000000000 and l_linestatus = 'X';
#delete from lineitem where l_orderkey <= 4500000000 and l_linestatus = 'X';
#delete from lineitem where l_orderkey <= 5000000000 and l_linestatus = 'X';
#delete from lineitem where l_orderkey <= 5500000000 and l_linestatus = 'X';
#delete from lineitem where l_orderkey <= 6000000000 and l_linestatus = 'X';
delete from lineitem where l_linestatus = 'X';
