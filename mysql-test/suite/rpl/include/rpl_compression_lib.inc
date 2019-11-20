--source include/master-slave.inc

connection slave;
set @save_comp_lib = @@global.slave_compression_lib;
set @save_slave_compress = @@global.replica_compressed_protocol;

set global replica_compressed_protocol=on;

--source include/stop_slave_io.inc
--source include/start_slave_io.inc

--let $zlib_before = query_get_value(show global status where variable_name='bytes_received', Value, 1)

connection master;

CREATE TABLE t1(c VARCHAR(1000));
# Insert large string to ensure replication needs to compress this
INSERT INTO t1 (c) VALUES(
'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'
'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb'
'ccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc'
'ddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd'
'eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee'
);

--source include/sync_slave_sql_with_master.inc

connection slave;
--let $zlib_after = query_get_value(show global status where variable_name='bytes_received', Value, 1)

--eval set global slave_compression_lib=$comp_lib
--source include/stop_slave_io.inc
--source include/start_slave_io.inc

--let $comp_before = query_get_value(show global status where variable_name='bytes_received', Value, 1)
--let $comp_out_before = query_get_value(show global status where variable_name='Compression_output_bytes', Value, 1)
--let $comp_in_before = query_get_value(show global status where variable_name='Compression_input_bytes', Value, 1)

connection master;

CREATE TABLE t2(c VARCHAR(1000));
# Insert large string to ensure replication needs to compress this
INSERT INTO t2 (c) VALUES(
'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'
'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb'
'ccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc'
'ddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd'
'eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee'
);

--source include/sync_slave_sql_with_master.inc

connection slave;
--let $comp_after = query_get_value(show global status where variable_name='bytes_received', Value, 1)
--let $comp_out_after = query_get_value(show global status where variable_name='Compression_output_bytes', Value, 1)
--let $comp_in_after = query_get_value(show global status where variable_name='Compression_input_bytes', Value, 1)
select c from t1;
select c from t2;

--disable_query_log
--eval select ($comp_out_after - $comp_out_before) <= ($comp_in_after - $comp_in_before) as "compressed"
--eval select ($zlib_after - $zlib_before) <> ($comp_after - $comp_before) as "compression_changed"
--enable_query_log

connection master;
drop table t1;
drop table t2;

connection slave;
set @@global.slave_compression_lib = @save_comp_lib;
set @@global.replica_compressed_protocol = @save_slave_compress;

--source include/rpl_end.inc
