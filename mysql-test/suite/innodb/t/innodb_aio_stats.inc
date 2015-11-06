#
# Check IO statistics
#
# Usage:
# let $page_size = <page size for the test>;
# let $expected_min_io_write_requests = <expected minimum number of write requests>
# let $expected_aio_calls = <expected number of asynchronouse I/O calls>
# let $expected_read_requests_lo = <expected low number of read requests>
# let $expected_read_requests_hi = <expected high number of read requests>
# let $expected_page_reads_lo = <expected low InnoDB page reads>
# let $expected_page_reads_hi = <expected high InnoDB page reads>
# let $expected_page_reads_index = <expected InnoDB index page reads>
# --source innodb_aio_stats.inc
#

--disable_warnings
DROP TABLE if exists t1;
--enable_warnings

# Create table.
CREATE TABLE t1 (a INT NOT NULL PRIMARY KEY AUTO_INCREMENT, b VARCHAR(256)) ENGINE=INNODB;

# Populate table.
INSERT INTO t1 VALUES (0, REPEAT('a',256));
INSERT INTO t1 SELECT 0, b FROM t1;
INSERT INTO t1 SELECT 0, b FROM t1;
INSERT INTO t1 SELECT 0, b FROM t1;
INSERT INTO t1 SELECT 0, b FROM t1;
INSERT INTO t1 SELECT 0, b FROM t1;
INSERT INTO t1 SELECT 0, b FROM t1;
INSERT INTO t1 SELECT 0, b FROM t1;
INSERT INTO t1 SELECT 0, b FROM t1;
INSERT INTO t1 SELECT 0, b FROM t1;
INSERT INTO t1 SELECT 0, b FROM t1;
INSERT INTO t1 SELECT 0, b FROM t1;
INSERT INTO t1 SELECT 0, b FROM t1;
INSERT INTO t1 SELECT 0, b FROM t1;
INSERT INTO t1 SELECT 0, b FROM t1;
INSERT INTO t1 SELECT 0, b FROM t1;
INSERT INTO t1 SELECT 0, b FROM t1;

--let $expected_min_write_bytes = ($page_size * $expected_min_io_write_requests)
eval select IO_WRITE_BYTES > $expected_min_write_bytes, IO_WRITE_REQUESTS > $expected_min_io_write_requests, IO_WRITE_WAIT_USECS > IO_WRITE_SVC_USECS, IO_WRITE_SLOW_IOS from information_schema.table_statistics where table_name = 't1';

eval select INNODB_PAGES_WRITTEN > $expected_min_io_write_requests, INNODB_PAGES_WRITTEN_INDEX > $expected_min_io_write_requests, INNODB_PAGES_WRITTEN_BLOB from information_schema.table_statistics where table_name = 't1';

--source include/restart_mysqld.inc

## The IO_READ_REQUESTS column doesn't exist in the information_schema.table_statistics
## table until some data has been read so do a bit of reading.
#SELECT COUNT(*) FROM t1 WHERE a = 1;

# Store the current state of some of the counters we are going to query later
let $aio_submitted_start = query_get_value(show global status like "innodb_buffered_aio_submitted", Value, 1);
let $io_read_bytes_start = 0;
let $io_read_requests_start = 0;
let $innodb_pages_read_start = 0;
let $innodb_pages_read_index_start = 0;

# Check to see if we have any reads - this can happen if the test is run in parallel.  If we do already have
# read information on this table, grab the current values to subtract out later.
let $have_reads = query_get_value(select count(*) as Value from information_schema.table_statistics where table_name = 't1', Value, 1);
if ($have_reads == 1) {
  let $io_read_bytes_start = query_get_value(select IO_READ_BYTES from information_schema.table_statistics where table_name = 't1', IO_READ_BYTES, 1);
  let $io_read_requests_start = query_get_value(select IO_READ_REQUESTS from information_schema.table_statistics where table_name = 't1', IO_READ_REQUESTS, 1);
  let $innodb_pages_read_start = query_get_value(select INNODB_PAGES_READ from information_schema.table_statistics where table_name = 't1', INNODB_PAGES_READ, 1);
  let $innodb_pages_read_index_start = query_get_value(select INNODB_PAGES_READ_INDEX from information_schema.table_statistics where table_name = 't1', INNODB_PAGES_READ_INDEX, 1);
}

--disable_result_log
select * from t1;
--enable_result_log

select count(*) from t1;

--echo # Verify innodb_buffered_aio_submitted: expect $expected_aio_calls asynchronous I/O calls submitted from previous queries
let $aio_submitted_end = query_get_value(show global status like "innodb_buffered_aio_submitted", Value, 1);
--disable_query_log
eval select $aio_submitted_end - $aio_submitted_start as "Innodb_buffered_aio_submitted";

--let $expected_read_bytes_lo = ($page_size * $expected_read_requests_lo)
--let $expected_read_bytes_hi = ($page_size * $expected_read_requests_hi)
--echo # Verify IO_READ_* stats: expect $expected_read_bytes_lo-$expected_read_bytes_hi bytes read and $expected_read_requests_lo-$expected_read_requests_hi read requests
eval select (IO_READ_BYTES - $io_read_bytes_start) >= $expected_read_bytes_lo as "IO_READ_BYTES_LO", (IO_READ_BYTES - $io_read_bytes_start) <= $expected_read_bytes_hi as "IO_READ_BYTES_HI", (IO_READ_REQUESTS - $io_read_requests_start) >= $expected_read_requests_lo as "IO_READ_REQUESTS_LO", (IO_READ_REQUESTS - $io_read_requests_start) <= $expected_read_requests_hi as "IO_READ_REQUESTS_HI", IO_READ_SVC_USECS < IO_READ_WAIT_USECS, IO_READ_SVC_USECS_MAX < IO_READ_WAIT_USECS_MAX, IO_READ_SLOW_IOS from information_schema.table_statistics where table_name = 't1';

--echo # Verify INNODB_PAGES_* stats: expect $expected_page_reads_lo-$expected_page_reads_hi InnoDB pages read and $expected_page_reads_index InnoDB index pages read and 0 InnoDB blob pages read
eval select (INNODB_PAGES_READ - $innodb_pages_read_start) >= $expected_page_reads_lo as "INNODB_PAGES_READ_LO", (INNODB_PAGES_READ - $innodb_pages_read_start) <= $expected_page_reads_hi as "INNODB_PAGES_READ_HI", (INNODB_PAGES_READ_INDEX - $innodb_pages_read_index_start) = $expected_page_reads_index as "INNODB_PAGES_READ_INDEX", INNODB_PAGES_READ_BLOB from information_schema.table_statistics where table_name = 't1';
--enable_query_log

DROP TABLE t1;
