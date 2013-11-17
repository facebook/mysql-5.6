--disable_warnings
DROP TABLE IF EXISTS t1;
--enable_warnings

# create the actual table
CREATE TABLE t1(id INT AUTO_INCREMENT PRIMARY KEY,
                msg_prefix VARCHAR(255),
                msg VARCHAR(255),
                msg_length int,
                msg_checksum varchar(128),
                KEY msg_i(msg_prefix))
ENGINE=INNODB
ROW_FORMAT=COMPRESSED
COMPRESSION_FLAGS=0x6
KEY_BLOCK_SIZE=1;

--source suite/innodb_stress/include/innodb_stress.inc

--connection slave
start slave;
--connection master

CREATE TABLE t1(id INT AUTO_INCREMENT PRIMARY KEY,
                msg_prefix VARCHAR(255),
                msg VARCHAR(255),
                msg_length int,
                msg_checksum varchar(128),
                KEY msg_i(msg_prefix))
ENGINE=INNODB
ROW_FORMAT=COMPRESSED
COMPRESSION=BZIP
KEY_BLOCK_SIZE=1;

--source suite/innodb_stress/include/innodb_stress.inc

--connection slave
start slave;
--connection master

CREATE TABLE t1(id INT AUTO_INCREMENT PRIMARY KEY,
                msg_prefix VARCHAR(255),
                msg VARCHAR(255),
                msg_length int,
                msg_checksum varchar(128),
                KEY msg_i(msg_prefix))
ENGINE=INNODB
ROW_FORMAT=COMPRESSED
COMPRESSION=LZMA
KEY_BLOCK_SIZE=1;

--source suite/innodb_stress/include/innodb_stress.inc

--connection slave
start slave;
--connection master

CREATE TABLE t1(id INT AUTO_INCREMENT PRIMARY KEY,
                msg_prefix VARCHAR(255),
                msg VARCHAR(255),
                msg_length int,
                msg_checksum varchar(128),
                KEY msg_i(msg_prefix))
ENGINE=INNODB
ROW_FORMAT=COMPRESSED
COMPRESSION=SNAPPY
KEY_BLOCK_SIZE=1;

--source suite/innodb_stress/include/innodb_stress.inc

--connection slave
start slave;
--connection master

CREATE TABLE t1(id INT AUTO_INCREMENT PRIMARY KEY,
                msg_prefix VARCHAR(255),
                msg VARCHAR(255),
                msg_length int,
                msg_checksum varchar(128),
                KEY msg_i(msg_prefix))
ENGINE=INNODB
ROW_FORMAT=COMPRESSED
COMPRESSION=QUICKLZ
KEY_BLOCK_SIZE=1;

--source suite/innodb_stress/include/innodb_stress.inc

--connection slave
start slave;
--connection master

CREATE TABLE t1(id INT AUTO_INCREMENT PRIMARY KEY,
                msg_prefix VARCHAR(255),
                msg VARCHAR(255),
                msg_length int,
                msg_checksum varchar(128),
                KEY msg_i(msg_prefix))
ENGINE=INNODB
ROW_FORMAT=COMPRESSED
COMPRESSION=LZ4
KEY_BLOCK_SIZE=1;

--source suite/innodb_stress/include/innodb_stress.inc

