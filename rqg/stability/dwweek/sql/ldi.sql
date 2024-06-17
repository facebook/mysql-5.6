set rocksdb_bulk_load=1;
load data infile "/tmp/ldisource.txt" into table lineitem fields terminated by "|";
set rocksdb_bulk_load=0;
