################################################################################
###
### Create and populate tables
###

--disable_warnings
drop table if exists t1, t2, t3, t6, t7, t8;
--enable_warnings

##
## For better testing coverages:
## t1, t2, t3: have identical schemas except the names
## t1, t2, t3: both doc11 and doc12 have null documents, non-existence document paths, and unexpected types
##
## For basic tests:
## t6, t7, t8: have identical schemas except the names
## t6, t7, t8: neither doc11 nor doc12 has null documents, non-existence document paths, or unexpected types
##


create table t1 (
  a1 int primary key,
  b1 int,
  c1 double,
  d1 char(10),
  str1 mediumtext,
  doc11 document,
  doc12 document default null,

  # regular keys
  key b1_a1        (b1, a1),
  key str1_a1      (str1(3)),

  # single column keys
  key doc11_bool   (doc11.bool as bool),
  key doc11_int    (doc11.int as int),
  key doc11_double (doc11.double as double),
  key doc11_string (doc11.string as string(3)),
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
  d2 char(10),
  str2 mediumtext,
  doc21 document,
  doc22 document,

  # regular keys
  key b2_a2        (b2, a2),
  key str2_a2      (str2(2)),

  # single column keys
  key doc21_bool   (doc21.bool as bool),
  key doc21_int    (doc21.int as int),
  key doc21_double (doc21.double as double),
  key doc21_string (doc21.string as string(2)),
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
  d3 char(10),
  str3 mediumtext,
  doc31 document,
  doc32 document,

  # regular keys
  key b3_a3        (b3, a3),
  key str3_a3      (str3(3)),

  # single column keys
  key doc31_bool   (doc31.bool as bool),
  key doc31_int    (doc31.int as int),
  key doc31_double (doc31.double as double),
  key doc31_string (doc31.string as string(3)),
  key doc31_nested (doc31.k1.k2.k3.int as int),
  key doc32_int    (doc32.int as int),
  key doc32_double (doc32.double as double),

  # multi-column keys
  key doc31_b3 (doc31.int as int, b3),
  key doc31_c3 (doc31.double as double, c3),
  key doc31_d3 (doc31.int as int, d3),
  key b3_doc31 (b3, doc31.int as int)
) engine = innodb;

create table t6 (
  a6 int primary key,
  b6 int,
  c6 double,
  d6 char(10),
  str6 mediumtext,
  doc61 document,
  doc62 document,

  # regular keys
  key b6_a6        (b6, a6),
  key str6_a6      (str6(10)),

  # single column keys
  key doc61_bool   (doc61.bool as bool),
  key doc61_int    (doc61.int as int),
  key doc61_double (doc61.double as double),
  key doc61_string (doc61.string as string(10)),
  key doc61_nested (doc61.k1.k2.k3.int as int),
  key doc62_int    (doc62.int as int),
  key doc62_double (doc62.double as double),

  # multi-column keys
  key doc61_b6 (doc61.int as int, b6),
  key doc61_c6 (doc61.double as double, c6),
  key doc61_d6 (doc61.int as int, d6),
  key b6_doc61 (b6, doc61.int as int)
) engine = innodb;

create table t7 (
  a7 int primary key,
  b7 int,
  c7 double,
  d7 char(10),
  str7 mediumtext,
  doc71 document,
  doc72 document,

  # regular keys
  key b7_a7        (b7, a7),
  key str7_a7      (str7(10)),

  # single column keys
  key doc71_bool   (doc71.bool as bool),
  key doc71_int    (doc71.int as int),
  key doc71_double (doc71.double as double),
  key doc71_string (doc71.string as string(10)),
  key doc71_nested (doc71.k1.k2.k3.int as int),
  key doc72_int    (doc72.int as int),
  key doc72_double (doc72.double as double),

  # multi-column keys
  key doc71_b7 (doc71.int as int, b7),
  key doc71_c7 (doc71.double as double, c7),
  key doc71_d7 (doc71.int as int, d7),
  key b7_doc71 (b7, doc71.int as int)
) engine = innodb;

create table t8 (
  a8 int primary key,
  b8 int,
  c8 double,
  d8 char(10),
  str8 mediumtext,
  doc81 document,
  doc82 document,

  # regular keys
  key b8_a8        (b8, a8),
  key str8_a8      (str8(10)),

  # single column keys
  key doc81_bool   (doc81.bool as bool),
  key doc81_int    (doc81.int as int),
  key doc81_double (doc81.double as double),
  key doc81_string (doc81.string as string(10)),
  key doc81_nested (doc81.k1.k2.k3.int as int),
  key doc82_int    (doc82.int as int),
  key doc82_double (doc82.double as double),

  # multi-column keys
  key doc81_b8 (doc81.int as int, b8),
  key doc81_c8 (doc81.double as double, c8),
  key doc81_d8 (doc81.int as int, d8),
  key b8_doc81 (b8, doc81.int as int)
) engine = innodb;


##
## t1: both doc11 and doc12 have null documents, non-existence document paths, and unexpected types
##

insert into t1 values
(10, 10, 10.1,  'jjj', 'jjj', '{"bool":false,     "int":10,      "double":"10.1", "string":"",        "k1":{"k2":{"k3":{"int":10}}},"id":10}', '{"int":10, "double":10.1}'),
(11, 11, 11.11, 'kkk', 'kkk', '{"bool":0,         "int":"11.111","double":true,   "string":"k",       "k1":{"k2":{"k3":{"int":11}}},"id":11}', '{"__t":11, "double":11.11}'),
(1,  1,  1.1,   'aaa', 'aaa', '{"bool":true,      "__t":1,       "double":1,      "string":"aaaaaaa", "k1":{"k2":{"k3":{"int":1}}}, "id":1}',  '{"int":1,  "double":1.1}'),
(2,  2,  2.2,   'bbb', 'bbb', '{"bool":"bbb",     "int":true,    "double":"2.2",  "string":2.2,       "k1":{"k2":{"k3":{"int":2}}}, "id":2}',  '{"int":2,  "___ble":2.2}'),
(3,  3,  3.3,   'ccc', 'ccc', '{"__ol":true,      "int":3,       "double":3.3,    "string":true,      "k1":{"k2":{"k3":{"int":3}}}, "id":3}',  '{"i__":3,  "dou___":3.3}'),
(5,  5,  5.5,   'eee', 'eee', '{"bool":false,     "int":5,       "double":5.5,    "string":"eee",     "k1":{"k2":{"k3":{"int":5}}}, "id":5}',  '{"int":5,  "double":5.5}'),
(8,  8,  8.8,   'hhh', 'hhh', '{"bool":123456789, "int":8,       "double":8.8,    "string":"hhh",     "k1":{"k2":{"k3":{"int":8}}}, "id":8}',  '{"int":8,  "double":8.8}'),
(9,  9,  9.9,   'iii', 'iii', '{"bool":null,      "int":null,    "double":null,   "string":null,      "k1":null}', null);

insert into t1 (a1, b1, c1, d1, str1, doc11) values
(12, 12, 12.12, 'lll', 'lll', '{"bool":2.5,       "int":false,   "double":false,  "string":12,        "k1":{"k2":{"k3":{"int":12}}},"id":12}'),
(4,  4,  4.4,   'ddd', 'ddd', '{"bool":"ddddddd", "int":"44444", "___ble":4.4,    "string":false,     "k1":{"k2":{"k3":{"int":4}}}, "id":4}'),
(7,  7,  7.7,   'ggg', 'ggg', '{"bool":"",        "int":7.777,   "double":false,  "___ing":"gg",      "k1":{"k2":{"k3":{"int":7}}}, "id":7}');

insert into t1 (a1, b1, c1, d1, str1) values
(6,  6,  6.6,   'fff', 'fff');

analyze table t1;

##
## t2: both doc21 and doc22 have null documents, non-existence document paths, and unexpected types
##

insert into t2 values
(10, 10, 10.1,  'jjj', 'jjj', '{"bool":false,     "int":10,      "double":"10.1", "string":"",        "k1":{"k2":{"k3":{"int":10}}},"id":10}', '{"int":10, "double":10.1}'),
(11, 11, 11.11, 'kkk', 'kkk', '{"bool":0,         "int":"11.111","double":true,   "string":"k",       "k1":{"k2":{"k3":{"int":11}}},"id":11}', '{"__t":11, "double":11.11}'),
(12, 12, 12.12, 'lll', 'lll', null,                                                                                                            null),
(1,  1,  1.1,   'aaa', 'aaa', '{"__ol":true,      "int":1,       "double":1,      "string":"aaaaaaa", "k1":{"k2":{"k3":{"int":1}}}, "id":1}',  '{"int":1,  "double":1.1}'),
(2,  2,  2.2,   'bbb', 'bbb', '{"bool":"bbb",     "int":true,    "double":"2.2",  "string":2.2,       "k1":{"k2":{"k3":{"int":2}}}, "id":2}',  '{"int":2,  "___ble":2.2}'),
(3,  3,  3.3,   'ccc', 'ccc', '{"bool":true,      "__t":3,       "___ble":3.3,    "string":true,      "k1":{"k2":{"k3":{"int":3}}}, "id":3}',  '{"i__":3,  "dou___":3.3}'),
(4,  4,  4.4,   'ddd', 'ddd', '{"bool":"ddddddd", "int":4,       "double":null,   "string":false,     "k1":{"k2":{"k3":{"int":4}}}, "id":4}',  null),
(5,  5,  5.5,   'eee', 'eee', null,                                                                                                            '{"int":5,  "double":5.5}'),
(6,  6,  6.6,   'fff', 'fff', '{"bool":false,     "int":"66666", "double":6,      "string":null,      "k1":{"k2":{"k3":{"int":6}}}, "id":6}',  '{"int":6,  "double":6.6}'),
(7,  7,  7.7,   'ggg', 'ggg', '{"bool":false,     "int":7,       "double":false,  "___ing":"g",       "k1":{"k2":{"k3":{"int":7}}}, "id":7}',  null),
(8,  8,  8.8,   'hhh', 'hhh', '{"bool":123456789, "int":null,    "double":8.8,    "string":"hhh",     "k1":{"k2":{"k3":{"int":8}}}, "id":8}',  '{"int":8,  "double":8.8}'),
(9,  9,  9.9,   'iii', 'iii', '{"bool":false,     "int":9,       "double":9.9,    "string":"iii",     "k1":{"k2":{"k3":{"int":9}}}, "id":9}',  '{"int":9,  "double":9.9}');

analyze table t2;

##
## t3: both of doc31 and doc32 are null in some rows
##

insert into t3 values
(1,  1,  1.1,   'aaa', 'aaa', '{"bool":true,      "int":1,       "double":1.1,    "string":"aaa",     "k1":{"k2":{"k3":{"int":1}}}, "id":1}',  '{"int":1,  "double":1.1}'),
(2,  2,  2.2,   'bbb', 'bbb', '{"bool":false,     "int":2,       "double":2.2,    "string":"bbb",     "k1":{"k2":{"k3":{"int":2}}}, "id":2}',  '{"int":2,  "double":2.2}'),
(8,  8,  8.8,   'hhh', 'hhh', '{"bool":true,      "int":8,       "double":8.8,    "string":"hhh",     "k1":{"k2":{"k3":{"int":8}}}, "id":8}',  '{"int":8,  "double":8.8}'),
(10, 10, 10.1,  'jjj', 'jjj', '{"bool":false,     "int":10,      "double":10.1,   "string":"jjj",     "k1":{"k2":{"k3":{"int":10}}},"id":10}', '{"int":10, "double":10.1}');


insert into t3 (a3, b3, c3, d3, str3) values
(11, 11, 11.11, 'kkk', 'kkk'),
(12, 12, 12.12, 'lll', 'lll'),
(3,  3,  3.3,   'ccc', 'ccc'),
(4,  4,  4.4,   'ddd', 'ddd'),
(5,  5,  5.5,   'eee', 'eee'),
(6,  6,  6.6,   'fff', 'fff'),
(7,  7,  7.7,   'ggg', 'ggg'),
(9,  9,  9.9,   'iii', 'iii');


analyze table t3;

##
## t6, t7, and t8: full data without null documents, non-existence document paths, or unexpected types
##

insert into t6 values
(10, 10, 10.10, 'jjj', 'jjj', '{"bool":true,   "int":10,  "double":10.10,  "string":"jjj",   "k1":{"k2":{"k3":{"int":10}}},"id":10}', '{"int":10, "double":10.10}'),
(11, 11, 11.11, 'kkk', 'kkk', '{"bool":false,  "int":11,  "double":11.11,  "string":"kkk",   "k1":{"k2":{"k3":{"int":11}}},"id":11}', '{"int":11, "double":11.11}'),
(12, 12, 12.12, 'lll', 'lll', '{"bool":true,   "int":12,  "double":12.12,  "string":"lll",   "k1":{"k2":{"k3":{"int":12}}},"id":12}', '{"int":12, "double":12.12}'),
(1,  1,  1.1,   'aaa', 'aaa', '{"bool":false,  "int":1,   "double":1.1,    "string":"aaa",   "k1":{"k2":{"k3":{"int":1}}}, "id":1}',  '{"int":1,  "double":1.1}'),
(2,  2,  2.2,   'bbb', 'bbb', '{"bool":true,   "int":2,   "double":2.2,    "string":"bbb",   "k1":{"k2":{"k3":{"int":2}}}, "id":2}',  '{"int":2,  "double":2.2}'),
(3,  3,  3.3,   'ccc', 'ccc', '{"bool":false,  "int":3,   "double":3.3,    "string":"ccc",   "k1":{"k2":{"k3":{"int":3}}}, "id":3}',  '{"int":3,  "double":3.3}'),
(4,  4,  4.4,   'ddd', 'ddd', '{"bool":true,   "int":4,   "double":4.4,    "string":"ddd",   "k1":{"k2":{"k3":{"int":4}}}, "id":4}',  '{"int":4,  "double":4.4}'),
(5,  5,  5.5,   'eee', 'eee', '{"bool":false,  "int":5,   "double":5.5,    "string":"eee",   "k1":{"k2":{"k3":{"int":5}}}, "id":5}',  '{"int":5,  "double":5.5}'),
(6,  6,  6.6,   'fff', 'fff', '{"bool":true,   "int":6,   "double":6.6,    "string":"fff",   "k1":{"k2":{"k3":{"int":6}}}, "id":6}',  '{"int":6,  "double":6.6}'),
(7,  7,  7.7,   'ggg', 'ggg', '{"bool":false,  "int":7,   "double":7.7,    "string":"ggg",   "k1":{"k2":{"k3":{"int":7}}}, "id":7}',  '{"int":7,  "double":7.7}'),
(8,  8,  8.8,   'hhh', 'hhh', '{"bool":true,   "int":8,   "double":8.8,    "string":"hhh",   "k1":{"k2":{"k3":{"int":8}}}, "id":8}',  '{"int":8,  "double":8.8}'),
(9,  9,  9.9,   'iii', 'iii', '{"bool":false,  "int":9,   "double":9.9,    "string":"iii",   "k1":{"k2":{"k3":{"int":9}}}, "id":9}',  '{"int":9,  "double":9.9}');

analyze table t6;

insert into t7 values
(10, 10, 10.10, 'jjj', 'jjj', '{"bool":true,   "int":10,  "double":10.10,  "string":"jjj",   "k1":{"k2":{"k3":{"int":10}}},"id":10}', '{"int":10, "double":10.10}'),
(11, 11, 11.11, 'kkk', 'kkk', '{"bool":false,  "int":11,  "double":11.11,  "string":"kkk",   "k1":{"k2":{"k3":{"int":11}}},"id":11}', '{"int":11, "double":11.11}'),
(12, 12, 12.12, 'lll', 'lll', '{"bool":true,   "int":12,  "double":12.12,  "string":"lll",   "k1":{"k2":{"k3":{"int":12}}},"id":12}', '{"int":12, "double":12.12}'),
(1,  1,  1.1,   'aaa', 'aaa', '{"bool":false,  "int":1,   "double":1.1,    "string":"aaa",   "k1":{"k2":{"k3":{"int":1}}}, "id":1}',  '{"int":1,  "double":1.1}'),
(2,  2,  2.2,   'bbb', 'bbb', '{"bool":true,   "int":2,   "double":2.2,    "string":"bbb",   "k1":{"k2":{"k3":{"int":2}}}, "id":2}',  '{"int":2,  "double":2.2}'),
(3,  3,  3.3,   'ccc', 'ccc', '{"bool":false,  "int":3,   "double":3.3,    "string":"ccc",   "k1":{"k2":{"k3":{"int":3}}}, "id":3}',  '{"int":3,  "double":3.3}'),
(4,  4,  4.4,   'ddd', 'ddd', '{"bool":true,   "int":4,   "double":4.4,    "string":"ddd",   "k1":{"k2":{"k3":{"int":4}}}, "id":4}',  '{"int":4,  "double":4.4}'),
(5,  5,  5.5,   'eee', 'eee', '{"bool":false,  "int":5,   "double":5.5,    "string":"eee",   "k1":{"k2":{"k3":{"int":5}}}, "id":5}',  '{"int":5,  "double":5.5}'),
(6,  6,  6.6,   'fff', 'fff', '{"bool":true,   "int":6,   "double":6.6,    "string":"fff",   "k1":{"k2":{"k3":{"int":6}}}, "id":6}',  '{"int":6,  "double":6.6}'),
(7,  7,  7.7,   'ggg', 'ggg', '{"bool":false,  "int":7,   "double":7.7,    "string":"ggg",   "k1":{"k2":{"k3":{"int":7}}}, "id":7}',  '{"int":7,  "double":7.7}'),
(8,  8,  8.8,   'hhh', 'hhh', '{"bool":true,   "int":8,   "double":8.8,    "string":"hhh",   "k1":{"k2":{"k3":{"int":8}}}, "id":8}',  '{"int":8,  "double":8.8}'),
(9,  9,  9.9,   'iii', 'iii', '{"bool":false,  "int":9,   "double":9.9,    "string":"iii",   "k1":{"k2":{"k3":{"int":9}}}, "id":9}',  '{"int":9,  "double":9.9}');

analyze table t7;

insert into t8 values
(10, 10, 10.10, 'jjj', 'jjj', '{"bool":true,   "int":10,  "double":10.10,  "string":"jjj",   "k1":{"k2":{"k3":{"int":10}}},"id":10}', '{"int":10, "double":10.10}'),
(11, 11, 11.11, 'kkk', 'kkk', '{"bool":false,  "int":11,  "double":11.11,  "string":"kkk",   "k1":{"k2":{"k3":{"int":11}}},"id":11}', '{"int":11, "double":11.11}'),
(12, 12, 12.12, 'lll', 'lll', '{"bool":true,   "int":12,  "double":12.12,  "string":"lll",   "k1":{"k2":{"k3":{"int":12}}},"id":12}', '{"int":12, "double":12.12}'),
(1,  1,  1.1,   'aaa', 'aaa', '{"bool":false,  "int":1,   "double":1.1,    "string":"aaa",   "k1":{"k2":{"k3":{"int":1}}}, "id":1}',  '{"int":1,  "double":1.1}'),
(2,  2,  2.2,   'bbb', 'bbb', '{"bool":true,   "int":2,   "double":2.2,    "string":"bbb",   "k1":{"k2":{"k3":{"int":2}}}, "id":2}',  '{"int":2,  "double":2.2}'),
(3,  3,  3.3,   'ccc', 'ccc', '{"bool":false,  "int":3,   "double":3.3,    "string":"ccc",   "k1":{"k2":{"k3":{"int":3}}}, "id":3}',  '{"int":3,  "double":3.3}'),
(4,  4,  4.4,   'ddd', 'ddd', '{"bool":true,   "int":4,   "double":4.4,    "string":"ddd",   "k1":{"k2":{"k3":{"int":4}}}, "id":4}',  '{"int":4,  "double":4.4}'),
(5,  5,  5.5,   'eee', 'eee', '{"bool":false,  "int":5,   "double":5.5,    "string":"eee",   "k1":{"k2":{"k3":{"int":5}}}, "id":5}',  '{"int":5,  "double":5.5}'),
(6,  6,  6.6,   'fff', 'fff', '{"bool":true,   "int":6,   "double":6.6,    "string":"fff",   "k1":{"k2":{"k3":{"int":6}}}, "id":6}',  '{"int":6,  "double":6.6}'),
(7,  7,  7.7,   'ggg', 'ggg', '{"bool":false,  "int":7,   "double":7.7,    "string":"ggg",   "k1":{"k2":{"k3":{"int":7}}}, "id":7}',  '{"int":7,  "double":7.7}'),
(8,  8,  8.8,   'hhh', 'hhh', '{"bool":true,   "int":8,   "double":8.8,    "string":"hhh",   "k1":{"k2":{"k3":{"int":8}}}, "id":8}',  '{"int":8,  "double":8.8}'),
(9,  9,  9.9,   'iii', 'iii', '{"bool":false,  "int":9,   "double":9.9,    "string":"iii",   "k1":{"k2":{"k3":{"int":9}}}, "id":9}',  '{"int":9,  "double":9.9}');

analyze table t8;


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
