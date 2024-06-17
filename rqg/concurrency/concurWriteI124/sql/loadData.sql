set rocksdb_bulk_load=1;
#
# load 10000 lineitem table
#
load data infile "/tmp/lineitem.tbl" into table lineitem fields terminated by "|";
#
# update l_linenumber to 0 and use it as a counter
#
update lineitem set l_linenumber = 0;
set rocksdb_bulk_load=0;