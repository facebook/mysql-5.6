---
docid: getting-started
title: Getting Started
layout: docs
permalink: /docs/getting-started/
---

## Overview

MyRocks is a MySQL storage engine that integrates with [RocksDB](http://rocksdb.org/). It provides improved flash storage performance through efficiencies in reading, writing, and storing data.

This Getting Started page provides information on MyRocks' [supported platforms](/docs/getting-started/#supported-platforms), [installation](/docs/getting-started/#installation) (including [creating your first table](/docs/getting-started/#create-a-rocksdb-table)), and [migrating from InnoDB](/docs/getting-started/#migrating-from-innodb-to-myrocks-in-production).

<h2>
  <a name="supported-platforms">Supported Platforms</a>
</h2>

The officially supported subset of platforms are:

* CentOS 6.8
* CentOS 7.2.x

Compiler toolsets we verify our builds with:

* gcc 4.8.1
* gcc 4.9.0
* gcc 5.4.0
* gcc 6.1.0
* Clang 3.9.0

Best effort is made to support the following OSs:

* Ubuntu 14.04.4 LTS
* Ubuntu 15.10
* Ubuntu 16.04 LTS

<br />

<h2>
  <a name="installation">Installation</a>
</h2>

### 1. Build MyRocks from Source

##### *Setting up Prerequisites*

On a fresh AWS Ubuntu 16.04.3 LTS instance:

```bash
sudo apt-get update
sudo apt-get install g++ cmake libbz2-dev libaio-dev bison \
zlib1g-dev libsnappy-dev libgflags-dev libreadline6-dev libncurses5-dev \
libssl-dev liblz4-dev libboost-dev gdb git
```

On Fedora and Redhat:

```bash
sudo yum install cmake gcc-c++ bzip2-devel libaio-devel bison \
zlib-devel snappy-devel
sudo yum install gflags-devel readline-devel ncurses-devel \
openssl-devel lz4-devel gdb git
```

Then set up the Git repository:

```bash
git clone https://github.com/facebook/mysql-5.6.git
cd mysql-5.6
git submodule init
git submodule update
cmake . -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWITH_SSL=system \
-DWITH_ZLIB=bundled -DMYSQL_MAINTAINER_MODE=0 -DENABLED_LOCAL_INFILE=1 \
-DENABLE_DTRACE=0 -DCMAKE_CXX_FLAGS="-march=native"
make -j8
```

##### *Different Build Types*

If you need a debug build, run CMake as follows:

```bash
cmake . -DCMAKE_BUILD_TYPE=Debug -DWITH_SSL=system \
-DWITH_ZLIB=bundled -DMYSQL_MAINTAINER_MODE=1 -DENABLE_DTRACE=0
```

If you want to produce a TSan build, you can add the following option to the CMake command-line:

```bash
-DWITH_TSAN=1
```

If you want to produce a UBSan build, you can add the following option to the CMake command-line:

```bash
-DWITH_UBSAN=1
```

If you want to build with Clang (verified on Ubuntu 16.04 LTS), you can add the following switches to the CMake command-line:

```bash
-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
```

##### *Adding Compression Libraries*

RocksDB itself supports multiple compression algorithms. By default, MyRocks only links Zlib, but you can add the Snappy, BZip2, LZ4, and ZSTD libraries by setting the environment variables to support them at compile time.

```bash
# assume libsnappy.a is located at /usr/lib/
export WITH_SNAPPY=/usr
cmake ...
make ...
```

You need to set WITH_BZ2, WITH_LZ4, WITH_ZSTD to support BZip2, LZ4, and ZSTD.

##### *Running MTR Tests*

```bash
cd mysql-test
./mysql-test-run.pl --mem --async-client --parallel=16 --fast \
--max-test-fail=1000 --retry=0 --force --mysqld=--rocksdb \
--mysqld=--default-storage-engine=rocksdb --mysqld=--skip-innodb \
--mysqld=--default-tmp-storage-engine=MyISAM --suite=rocksdb
```

##### *Installing Linkbench*

```bash
sudo apt-get install openjdk-7-jdk maven
git clone https://github.com/facebook/linkbench.git
cd linkbench;
mvn clean package -P fast-test
```

### 2. Set up `my.cnf`

To enable a RocksDB storage engine, you need to set at least the following parameters in the my.cnf file:

```
[mysqld]
rocksdb
default-storage-engine=rocksdb
skip-innodb
default-tmp-storage-engine=MyISAM
collation-server=latin1_bin (or utf8_bin, binary)

log-bin
binlog-format=ROW
```

>If you want to use both InnoDB and MyRocks within the same instance, set `allow-multiple-engines` and remove `skip-innodb` in `my.cnf`. Using mixed storage engines is not recommended in production because it is not really transactional, but it's okay for experimental purposes.

Statement-based binary logging is allowed on a replication slave, but not on a master because MyRocks doesn't support next-key locking.

### 3. Initialize the database with `mysql_install_db`

```bash
mysql_install_db --defaults-file=/path/to/my.cnf
```

### 4. Start `mysqld`

```bash
mysqld_safe --defaults-file=/path/to/my.cnf
```

<h3>
  <a name="create-a-rocksdb-table">5. Create a RocksDB table</a>
</h3>

*Example*

```sql
CREATE TABLE `linktable` (
  `id1` bigint(20) unsigned NOT NULL DEFAULT '0',
  `id1_type` int(10) unsigned NOT NULL DEFAULT '0',
  `id2` bigint(20) unsigned NOT NULL DEFAULT '0',
  `id2_type` int(10) unsigned NOT NULL DEFAULT '0',
  `link_type` bigint(20) unsigned NOT NULL DEFAULT '0',
  `visibility` tinyint(3) NOT NULL DEFAULT '0',
  `data` varchar(255) NOT NULL DEFAULT '',
  `time` bigint(20) unsigned NOT NULL DEFAULT '0',
  `version` int(11) unsigned NOT NULL DEFAULT '0',
PRIMARY KEY (link_type, `id1`,`id2`) COMMENT 'cf_link_pk',
KEY `id1_type` (`id1`,`link_type`,`visibility`,`time`,`version`,`data`) COMMENT 'rev:cf_link_id1_type'
) ENGINE=RocksDB DEFAULT COLLATE=latin1_bin;
```

The example shows some important features and limitations in MyRocks. For limitations, please read [MyRocks Limitations](https://github.com/facebook/mysql-5.6/wiki/MyRocks-limitations) for details.

* MyRocks data is stored in RocksDB, per index basis. RocksDB internally allocates a *Column Family* to store indexes. By default, all data is stored in the *default* column family. You can change the column family by setting an index comment. In this example, Primary Key is stored in the `cf_link_pk` column family, and the `id1_type` index data is stored in the `rev:cf_link_id1_type` column family.
* MyRocks has a feature called *Reverse Column Family*. Reverse Column Family is useful if the index is mainly used for a descending scan (ORDER BY .. DESC). You can configure the Reverse Column Family by setting `rev:` before the column family name. In this example, `id1_type` belongs to the Reverse Column Family.

<br />

<h2>
  <a name="migrating-from-innodb-to-myrocks-in-production">Migrating from InnoDB to MyRocks in production</a>
</h2>

>If you want to use both InnoDB and MyRocks within the same instance, set `allow-multiple-engines` and remove `skip-innodb` in `my.cnf`. Using mixed storage engines is not recommended in production because it is not really transactional, but it's okay for experimental purposes.

There is no online migration framework to move data between storage engines, but you will obviously want this to happen without downtime, losing data, or returning inaccurate results.  You need to move logical data from the source MySQL server with the InnoDB engine and load it into the destination MySQL server with the MyRocks engine.

1. Create an empty MyRocks instance with MyRocks tables.
2. Copy all the database and table schemas from the source to the destination.
3. Dump each table to a file using `SELECT INTO OUTFILE`.
4. Send the files to the destination and load them using `LOAD DATA INFILE`.

To speed the loading process on the destination, it is recommended to use the following options:

```sql
--sql_log_bin=0
--foreign_key_checks=0
--unique_checks=0
--rocksdb_compaction_sequential_deletes=0
--rocksdb_compaction_sequential_deletes_window=0
--rocksdb_write_disable_wal=1
--rocksdb_bulk_load=1
--rocksdb_skip_unique_check=1
--rocksdb_commit_in_the_middle=1
--rocksdb_max_background_flushes=12
--rocksdb_max_background_compactions=12
--rocksdb_base_background_compactions=12
```

>Note the above options are safe in migration scenarios. However, it is not recommended to use them elsewhere.

<br />

## Further Documentation

Full documentation for MyRocks can be found in the [GitHub wiki](https://github.com/facebook/mysql-5.6/wiki).
