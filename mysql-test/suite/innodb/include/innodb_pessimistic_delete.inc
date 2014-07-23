--source include/not_valgrind.inc
--source include/have_innodb.inc

--disable_warnings
DROP TABLE IF EXISTS t1;
--enable_warnings

call mtr.add_suppression("InnoDB: Warning: innodb_zip_debug is enabled and this is a non-debug mysqld. Please disable it immediately if the machine is serving production traffic.");

# create the actual table
CREATE TABLE t1(`id` INT AUTO_INCREMENT PRIMARY KEY, msg VARCHAR(255))
ENGINE=INNODB
ROW_FORMAT=COMPRESSED
COMPRESSION=ZLIB
COMPACT_METADATA=1
KEY_BLOCK_SIZE=1;

let $num_inserts = 23;
let $idx = 1;
START TRANSACTION;
while ($num_inserts) {
  eval INSERT INTO t1 VALUES ($idx, REPEAT(PASSWORD($idx), 5));
  dec $num_inserts;
  inc $idx;
}
COMMIT;

SELECT * FROM t1;

DELETE FROM t1 WHERE `id` <= 4;

SELECT * FROM t1;

CHECKSUM TABLE t1;

DROP TABLE t1;
