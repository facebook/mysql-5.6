# Taken from pt-table-checksum. Compare the number of rows and table contents between instances.
connection master;
select count(*) as cnt, CONV(BIT_XOR(CAST(CRC32(CONCAT_WS('#',id1, id2,c1,c2,c3,c4,c5,c6,c7)) AS UNSIGNED)), 10, 16) AS crc FROM t1 FORCE INDEX(PRIMARY);
select count(*) as cnt, CONV(BIT_XOR(CAST(CRC32(CONCAT_WS('#',c1,c2,id1,id2)) AS UNSIGNED)), 10, 16) AS crc FROM t1 FORCE INDEX(i);
connection slave;
select count(*) as cnt, CONV(BIT_XOR(CAST(CRC32(CONCAT_WS('#',id1, id2,c1,c2,c3,c4,c5,c6,c7)) AS UNSIGNED)), 10, 16) AS crc FROM t1 FORCE INDEX(PRIMARY);
select count(*) as cnt, CONV(BIT_XOR(CAST(CRC32(CONCAT_WS('#',c1,c2,id1,id2)) AS UNSIGNED)), 10, 16) AS crc FROM t1 FORCE INDEX(i);
connection master;

