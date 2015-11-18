################################################################################
###
### Create and populate tables
###

--disable_warnings
drop table if exists t1, t2, t3, t4;
--enable_warnings

create table t1 (
  a1 int primary key,
  b1 int,
  c1 double,
  d1 char(100),
  doc11 document not null,
  doc12 document,

  # regular keys
  key b1_a1         (b1, a1),

  # single column keys
  key doc11_bool   (doc11.bool as bool),
  key doc11_int    (doc11.int as int),
  key doc11_double (doc11.double as double),
  key doc11_string (doc11.string as string(100)),
  key doc11_binary (doc11.binary as binary(100)),
  key doc11_nested (doc11.k1.k2.k3.int as int),
  key doc12_int    (doc12.int as int),
  key doc12_double (doc12.double as double),

  # multi-column keys
  key doc11_b1 (doc11.int as int, b1),
  key doc11_c1 (doc11.double as double, c1),
  key doc11_d1 (doc11.int as int, d1),
  key b1_doc11 (b1, doc11.int as int)
) engine = innodb;

create table t2 (
  a2 int primary key,
  b2 int,
  c2 double,
  d2 char(100),
  doc21 document not null,
  doc22 document,

  # regular keys
  key b2_a2         (b2, a2),

  # single column keys
  key doc21_bool   (doc21.bool as bool),
  key doc21_int    (doc21.int as int),
  key doc21_double (doc21.double as double),
  key doc21_string (doc21.string as string(100)),
  key doc21_binary (doc21.binary as binary(100)),
  key doc21_nested (doc21.k1.k2.k3.int as int),
  key doc22_int    (doc22.int as int),
  key doc22_double (doc22.double as double),

  # multi-column keys
  key doc21_b2 (doc21.int as int, b2),
  key doc21_c2 (doc21.double as double, c2),
  key doc21_d2 (doc21.int as int, d2),
  key b2_doc21 (b2, doc21.int as int)
) engine = innodb;

create table t3 (
  a3 int primary key,
  b3 int,
  c3 double,
  d3 char(100),
  doc31 document not null,
  doc32 document,

  # regular keys
  key b3_a3         (b3, a3),

  # single column keys
  key doc31_bool   (doc31.bool as bool),
  key doc31_int    (doc31.int as int),
  key doc31_double (doc31.double as double),
  key doc31_string (doc31.string as string(100)),
  key doc31_binary (doc31.binary as binary(100)),
  key doc31_nested (doc31.k1.k2.k3.int as int),
  key doc32_int    (doc32.int as int),
  key doc32_double (doc32.double as double),

  # multi-column keys
  key doc31_b3 (doc31.int as int, b3),
  key doc31_c3 (doc31.double as double, c3),
  key doc31_d3 (doc31.int as int, d3),
  key b3_doc31 (b3, doc31.int as int)
) engine = innodb;

create table t4 (
  a4 int primary key,
  b4 int,
  c4 double,
  d4 char(100),
  doc41 document not null,
  doc42 document,

  # regular keys
  key b4        (b4),
  key b4_a4     (b4, a4),

  # single column keys
  key doc41_bool   (doc41.bool as bool),
  key doc41_int    (doc41.int as int),
  key doc41_double (doc41.double as double),
  key doc41_string (doc41.string as string(100)),
  key doc41_binary (doc41.binary as binary(100)),
  key doc41_nested (doc41.k1.k2.k3.int as int),
  key doc42_int    (doc42.int as int),
  key doc42_double (doc42.double as double),

  # multi-column keys
  key doc41_b4 (doc41.int as int, b4),
  key doc41_c4 (doc41.double as double, c4),
  key doc41_d4 (doc41.int as int, d4),

  key b4_doc41 (b4, doc41.int as int),

  key b4_c4 (b4, c4)

) engine = innodb;


insert into t1 values
(10, 10, 10.1, 'jjj', '{"bool":false, "int":10, "double":10.1, "string":"jjj", "binary":"jjj", "k1":{"k2":{"k3":{"int":10}}}, "id":10}', '{"int":10, "double":10.1}'),
(11, 11, 11.11, 'kkk', '{"bool":false, "int":11, "double":11.11, "string":"kkk", "binary":"kkk", "k1":{"k2":{"k3":{"int":11}}}, "id":11}', '{"int":11, "double":11.11}'),
(12, 12, 12.12, 'lll', '{"bool":false, "int":12, "double":12.12, "string":"lll", "binary":"lll", "k1":{"k2":{"k3":{"int":12}}}, "id":12}', '{"int":12, "double":12.12}'),
(1, 1, 1.1, 'aaa', '{"bool":true,  "int":1, "double":1.1, "string":"aaa", "binary":"aaa", "k1":{"k2":{"k3":{"int":1}}}, "id":1}', '{"int":1, "double":1.1}'),
(2, 2, 2.2, 'bbb', '{"bool":true,  "int":2, "double":2.2, "string":"bbb", "binary":"bbb", "k1":{"k2":{"k3":{"int":2}}}, "id":2}', '{"int":2, "double":2.2}'),
(3, 3, 3.3, 'ccc', '{"bool":true,  "int":3, "double":3.3, "string":"ccc", "binary":"ccc", "k1":{"k2":{"k3":{"int":3}}}, "id":3}', '{"int":3, "double":3.3}'),
(4, 4, 4.4, 'ddd', '{"bool":true,  "int":4, "double":4.4, "string":"ddd", "binary":"ddd", "k1":{"k2":{"k3":{"int":4}}}, "id":4}', '{"int":4, "double":4.4}'),
(5, 5, 5.5, 'eee', '{"bool":true,  "int":5, "double":5.5, "string":"eee", "binary":"eee", "k1":{"k2":{"k3":{"int":5}}}, "id":5}', '{"int":5, "double":5.5}'),
(6, 6, 6.6, 'fff', '{"bool":false, "int":6, "double":6.6, "string":"fff", "binary":"fff", "k1":{"k2":{"k3":{"int":6}}}, "id":6}', '{"int":6, "double":6.6}'),
(7, 7, 7.7, 'ggg', '{"bool":false, "int":7, "double":7.7, "string":"ggg", "binary":"ggg", "k1":{"k2":{"k3":{"int":7}}}, "id":7}', '{"int":7, "double":7.7}'),
(8, 8, 8.8, 'hhh', '{"bool":false, "int":8, "double":8.8, "string":"hhh", "binary":"hhh", "k1":{"k2":{"k3":{"int":8}}}, "id":8}', '{"int":8, "double":8.8}'),
(9, 9, 9.9, 'iii', '{"bool":false, "int":9, "double":9.9, "string":"iii", "binary":"iii", "k1":{"k2":{"k3":{"int":9}}}, "id":9}', '{"int":9, "double":9.9}');

analyze table t1;

insert into t2 values
(10, 10, 10.1, 'jjj', '{"bool":false, "int":10, "double":10.1, "string":"jjj", "binary":"jjj", "k1":{"k2":{"k3":{"int":10}}}, "id":10}', '{"int":10, "double":10.1}'),
(11, 11, 11.11, 'kkk', '{"bool":false, "int":11, "double":11.11, "string":"kkk", "binary":"kkk", "k1":{"k2":{"k3":{"int":11}}}, "id":11}', '{"int":11, "double":11.11}'),
(12, 12, 12.12, 'lll', '{"bool":false, "int":12, "double":12.12, "string":"lll", "binary":"lll", "k1":{"k2":{"k3":{"int":12}}}, "id":12}', '{"int":12, "double":12.12}'),
(1, 1, 1.1, 'aaa', '{"bool":true,  "int":1, "double":1.1, "string":"aaa", "binary":"aaa", "k1":{"k2":{"k3":{"int":1}}}, "id":1}', '{"int":1, "double":1.1}'),
(2, 2, 2.2, 'bbb', '{"bool":true,  "int":2, "double":2.2, "string":"bbb", "binary":"bbb", "k1":{"k2":{"k3":{"int":2}}}, "id":2}', '{"int":2, "double":2.2}'),
(3, 3, 3.3, 'ccc', '{"bool":true,  "int":3, "double":3.3, "string":"ccc", "binary":"ccc", "k1":{"k2":{"k3":{"int":3}}}, "id":3}', '{"int":3, "double":3.3}'),
(4, 4, 4.4, 'ddd', '{"bool":true,  "int":4, "double":4.4, "string":"ddd", "binary":"ddd", "k1":{"k2":{"k3":{"int":4}}}, "id":4}', '{"int":4, "double":4.4}'),
(5, 5, 5.5, 'eee', '{"bool":true,  "int":5, "double":5.5, "string":"eee", "binary":"eee", "k1":{"k2":{"k3":{"int":5}}}, "id":5}', '{"int":5, "double":5.5}'),
(6, 6, 6.6, 'fff', '{"bool":false, "int":6, "double":6.6, "string":"fff", "binary":"fff", "k1":{"k2":{"k3":{"int":6}}}, "id":6}', '{"int":6, "double":6.6}'),
(7, 7, 7.7, 'ggg', '{"bool":false, "int":7, "double":7.7, "string":"ggg", "binary":"ggg", "k1":{"k2":{"k3":{"int":7}}}, "id":7}', '{"int":7, "double":7.7}'),
(8, 8, 8.8, 'hhh', '{"bool":false, "int":8, "double":8.8, "string":"hhh", "binary":"hhh", "k1":{"k2":{"k3":{"int":8}}}, "id":8}', '{"int":8, "double":8.8}'),
(9, 9, 9.9, 'iii', '{"bool":false, "int":9, "double":9.9, "string":"iii", "binary":"iii", "k1":{"k2":{"k3":{"int":9}}}, "id":9}', '{"int":9, "double":9.9}');

analyze table t2;

insert into t3 values
(10, 10, 10.1, 'jjj', '{"bool":false, "int":10, "double":10.1, "string":"jjj", "binary":"jjj", "k1":{"k2":{"k3":{"int":10}}}, "id":10}', '{"int":10, "double":10.1}'),
(11, 11, 11.11, 'kkk', '{"bool":false, "int":11, "double":11.11, "string":"kkk", "binary":"kkk", "k1":{"k2":{"k3":{"int":11}}}, "id":11}', '{"int":11, "double":11.11}'),
(12, 12, 12.12, 'lll', '{"bool":false, "int":12, "double":12.12, "string":"lll", "binary":"lll", "k1":{"k2":{"k3":{"int":12}}}, "id":12}', '{"int":12, "double":12.12}'),
(1, 1, 1.1, 'aaa', '{"bool":true,  "int":1, "double":1.1, "string":"aaa", "binary":"aaa", "k1":{"k2":{"k3":{"int":1}}}, "id":1}', '{"int":1, "double":1.1}'),
(2, 2, 2.2, 'bbb', '{"bool":true,  "int":2, "double":2.2, "string":"bbb", "binary":"bbb", "k1":{"k2":{"k3":{"int":2}}}, "id":2}', '{"int":2, "double":2.2}'),
(3, 3, 3.3, 'ccc', '{"bool":true,  "int":3, "double":3.3, "string":"ccc", "binary":"ccc", "k1":{"k2":{"k3":{"int":3}}}, "id":3}', '{"int":3, "double":3.3}'),
(4, 4, 4.4, 'ddd', '{"bool":true,  "int":4, "double":4.4, "string":"ddd", "binary":"ddd", "k1":{"k2":{"k3":{"int":4}}}, "id":4}', '{"int":4, "double":4.4}'),
(5, 5, 5.5, 'eee', '{"bool":true,  "int":5, "double":5.5, "string":"eee", "binary":"eee", "k1":{"k2":{"k3":{"int":5}}}, "id":5}', '{"int":5, "double":5.5}'),
(6, 6, 6.6, 'fff', '{"bool":false, "int":6, "double":6.6, "string":"fff", "binary":"fff", "k1":{"k2":{"k3":{"int":6}}}, "id":6}', '{"int":6, "double":6.6}'),
(7, 7, 7.7, 'ggg', '{"bool":false, "int":7, "double":7.7, "string":"ggg", "binary":"ggg", "k1":{"k2":{"k3":{"int":7}}}, "id":7}', '{"int":7, "double":7.7}'),
(8, 8, 8.8, 'hhh', '{"bool":false, "int":8, "double":8.8, "string":"hhh", "binary":"hhh", "k1":{"k2":{"k3":{"int":8}}}, "id":8}', '{"int":8, "double":8.8}'),
(9, 9, 9.9, 'iii', '{"bool":false, "int":9, "double":9.9, "string":"iii", "binary":"iii", "k1":{"k2":{"k3":{"int":9}}}, "id":9}', '{"int":9, "double":9.9}');

analyze table t3;

insert into t4 values
(10, 10, 10.1, 'jjj', '{"bool":false, "int":10, "double":10.1, "string":"jjj", "binary":"jjj", "k1":{"k2":{"k3":{"int":10}}}, "id":10}', '{"int":10, "double":10.1}'),
(12, 12, 12.12, 'lll', '{"bool":false, "int":12, "double":12.12, "string":"lll", "binary":"lll", "k1":{"k2":{"k3":{"int":12}}}, "id":12}', '{"int":12, "double":12.12}'),
(1, 1, 1.1, 'aaa', '{"bool":true,  "int":1, "double":1.1, "string":"aaa", "binary":"aaa", "k1":{"k2":{"k3":{"int":1}}}, "id":1}', '{"int":1, "double":1.1}'),
(3, 3, 3.3, 'ccc', '{"bool":true,  "int":3, "double":3.3, "string":"ccc", "binary":"ccc", "k1":{"k2":{"k3":{"int":3}}}, "id":3}', '{"int":3, "double":3.3}'),
(6, 6, 6.6, 'fff', '{"bool":false, "int":6, "double":6.6, "string":"fff", "binary":"fff", "k1":{"k2":{"k3":{"int":6}}}, "id":6}', '{"int":6, "double":6.6}'),
(7, 7, 7.7, 'ggg', '{"bool":false, "int":7, "double":7.7, "string":"ggg", "binary":"ggg", "k1":{"k2":{"k3":{"int":7}}}, "id":7}', '{"int":7, "double":7.7}');

analyze table t4;

################################################################################
###
### ACCESS METHODS EXPLAINED
###

# "select *" on a document table should always be full table scan
explain extended select * from t1 use document keys;
select * from t1 use document keys;

# "select doc" should always be full table scan as well
# (i.e. no covering index on the whole document)
explain extended select doc11 from t1 use document keys;
select doc11 from t1 use document keys;
explain extended select doc12 from t1 use document keys;
select doc12 from t1 use document keys;

################################################################################
