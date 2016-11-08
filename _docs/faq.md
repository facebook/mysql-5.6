---
docid: support-faq
title: FAQ
layout: docs
permalink: /docs/support/faq.html
---

## What is MyRocks?

MyRocks is an open source project that integrates [RocksDB](http://rocksdb.org/) as a new MySQL storage engine.  

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
[Thoughts on MariaDB 10.3 from MariaDB Developers Meeting in Ansterdam, part 1](https://mariadb.org/thoughts-mariadb-server-10-3-mariadb-developers-meeting-amsterdam-part-1/)

## Why is MyRocks open sourced?

By sharing MyRocks with the community through open source, we hope that others can take advantage of these performance efficiencies.
