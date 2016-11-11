---
docid: support-faq
title: FAQ
layout: docs
permalink: /docs/support/faq.html
---

## What is MyRocks?

MyRocks came about because of a wanting to use MySQL features with [RocksDB](http://rocksdb.org/) implementations at Facebook. RocksDB is optimized for fast, low-latency storage, and MyRocks is aimed at keeping the storage savings efficient.

MyRock's efficiency focuses on better space efficiency, better write efficiency, and better read efficiency.  

* Better space efficiency means using less SSD storage.  
* Better write efficiency means SSD endurance.  
* Better read efficiency comes from more available IO capacity for handling queries.

The library is maintained by the Facebook Database Engineering Team and is based on the Oracle MySQL 5.6 database.

For more information about MyRocks implementation at Facebook, see [MyRocks: A space- and write-optimized MySQL database](https://code.facebook.com/posts/190251048047090/myrocks-a-space-and-write-optimized-mysql-database/.)

## How does performance compare?

In benchmark tests against 3 different instances -- MyRocks (compressed), InnoDB (uncompressed), and InnoDB (compressed, 8 KB page size) -- we found:

* MyRocks was 2x smaller than InnoDB (compressed) and 3.5x smaller than InnoDB (uncompressed).
* MyRocks also has a 10x lower storage write rate compared to InnoDB.

With SSD database storage, this means less space used and a higher endurance of the storage over time.

## How big is MyRocks adoption?

### Percona

Percona announced that is is bringing MyRocks to Percona Server to make MyRocks more accessible to users.

For more information:<br />
[Announcing MyRocks in Percona Server for MySQL](https://www.percona.com/blog/2016/10/24/announcing-myrocks-in-percona-server-for-mysql/)

### MariaDB

MyRocks will be included MariaDB 10.2.

For more information:<br />
[Thoughts on MariaDB 10.3 from MariaDB Developers Meeting in Amsterdam, part 1](https://mariadb.org/thoughts-mariadb-server-10-3-mariadb-developers-meeting-amsterdam-part-1/)

## Why is MyRocks open sourced?

By sharing MyRocks with the community through open source, we hope that others will benefit from the advantage of these performance efficiencies.
