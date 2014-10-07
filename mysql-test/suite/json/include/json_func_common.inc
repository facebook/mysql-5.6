# Description
# -----------
# Common test cases for tables containing json strings

#
# json_valid
#

# test on table
insert into test_json values
(null),
('{}'),
('{"k":"v"}'),
('{"k":{}}'),
('{"k":[]}'),
('{"k1":"v1","k2":true,"k3":3}'),
('{"k1":{"k2":"v2"}}'),
('{"key":[1,2,3]}'),
('[]'),
('[[[[[]]]]]'),
('[{}]'),
('[1,2,3]'),
('[1,false,"str",1.2]'),
('[{"k1":"v1"},2,3]'),
('[[1,2,3],2,3]'),
(''),
(123),
(1.23),
('abc'),
('{"key"}'),
('{1:value}'),
('{"key":"value"'),
('[1,2,3'),
('{}}'),
('[[]'),
('{[]}'),
('{"MySQL":"开源数据库"}'),
('{"开源数据库":"MySQL"}');

select json_valid(json),json from test_json order by 1,2;

truncate test_json;

#
# json_extract
#

# test on table
insert into test_json values
(null),
('{}'),
('{"k1":"v1"}'),
('{"k1":true}'),
('{"k1":false}'),
('{"k1":null}'),
('{"k1":12}'),
('{"k1":1.23}'),
('[1,2,3,4]'),
('{"k1":{"k2":{"k3":12.345}}}'),
('{"k1":"v1","k1":{"k2":"v2"}}'),
('{"k1":[1,{"k2":["v1",2,true]},3,false]}');

select length(json_extract(json,null)),json_extract(json,null),json from test_json order by 1,2,3;
select length(json_extract(json,'k1')),json_extract(json,'k1'),json from test_json order by 1,2,3;
select length(json_extract(json,'k1','k2')),json_extract(json,'k1','k2'),json from test_json order by 1,2,3;
select length(json_extract(json,'k1','k2','k3')),json_extract(json,'k1','k2','k3'),json from test_json order by 1,2,3;
select length(json_extract(json,0)),json_extract(json,0),json from test_json order by 1,2,3;
select length(json_extract(json,'1')),json_extract(json,'1'),json from test_json order by 1,2,3;
select length(json_extract(json,'k1',1)),json_extract(json,'k1',1),json from test_json order by 1,2,3;
select length(json_extract(json,'k1',1,'k2')),json_extract(json,'k1',1,'k2'),json from test_json order by 1,2,3;
select length(json_extract(json,'k1',1,'k2',0)),json_extract(json,'k1',1,'k2',0),json from test_json order by 1,2,3;

# test create table...select
truncate test_json;
insert into test_json values ('{"k1":[1,{"k2":["v1",2,true]},3,false]}');
create table test_json2 (json text) select json_extract(json,'k1',1,'k2',0) json from test_json;
show columns from test_json2;
select * from test_json2;
drop table test_json2;

truncate test_json;

# test multi-byte characters
insert into test_json values
('{"MySQL":"开源数据库"}'),
('{"开源数据库":"MySQL"}');

select length(json_extract(json,'MySQL')),json_extract(json,'MySQL'),json from test_json order by 1,2,3;
select length(json_extract(json,'开源数据库')),json_extract(json,'开源数据库'),json from test_json order by 1,2,3;

truncate test_json;

# test special case: one key is prefix of another
insert into test_json values ('{"k":1, "kk":2, "kkkk":4}');
select json_extract(json, 'k') k,
       json_extract(json, 'kk') kk,
       json_extract(json, 'kkk') kkk,
       json_extract(json, 'kkkk') kkkk
from test_json;

truncate test_json;

#
# json_extract_value
#

# test on table
insert into test_json values
(null),
('{}'),
('{"k1":"v1"}'),
('{"k1":true}'),
('{"k1":false}'),
('{"k1":null}'),
('{"k1":12}'),
('{"k1":1.23}');

select length(json_extract_value(json,null)),json_extract_value(json,null),json from test_json order by 1,2,3;
select length(json_extract_value(json,'k1')),json_extract_value(json,'k1'),json from test_json order by 1,2,3;
select length(json_extract_value(json,'k1','k2')),json_extract_value(json,'k1','k2'),json from test_json order by 1,2,3;

# json_extract_value in where clause
select json from test_json where json_extract_value(json,'k1') > 1 order by 1;

truncate test_json;

#
# json_contains_key
#

# test on table
insert into test_json values
(null),
('{}'),
('{"k1":"v1"}'),
('[1,2,3,4]'),
('{"k1":{"k2":{"k3":12.345}}}'),
('{"k1":"v1","k1":{"k2":"v2"}}'),
('{"k1":[1,{"k2":["v1",2,true]},3,false]}');

select json_contains_key(json,null),json from test_json order by 1,2;
select json_contains_key(json,'k1',null),json from test_json order by 1,2;
select json_contains_key(json,'k2'),json from test_json order by 1,2;
select json_contains_key(json,'k1'),json from test_json order by 1,2;
select json_contains_key(json,'k1','k2'),json from test_json order by 1,2;
select json_contains_key(json,'k1','k2','k3'),json from test_json order by 1,2;
select json_contains_key(json,0),json from test_json order by 1,2;
select json_contains_key(json,'1'),json from test_json order by 1,2;
select json_contains_key(json,'k1',1),json from test_json order by 1,2;
select json_contains_key(json,'k1',1,'k2'),json from test_json order by 1,2;
select json_contains_key(json,'k1',1,'k2',0),json from test_json order by 1,2;

truncate test_json;

# test multi-byte characters
insert into test_json values
('{"MySQL":"开源数据库"}'),
('{"开源数据库":"MySQL"}');

select length(json_contains_key(json,'MySQL')),json_contains_key(json,'MySQL'),json from test_json order by 1,2,3;
select length(json_contains_key(json,'开源数据库')),json_contains_key(json,'开源数据库'),json from test_json order by 1,2,3;

truncate test_json;

#
# json_array_length
#

# test on table
insert into test_json values
(null),
('[]'),
('[1]'),
('["str"]'),
('[null]'),
('[true,false]'),
('[1.234]'),
('[1,"str",null,true,false,1.2345]'),
('[[[[[[]]]]]]'),
('[[],[],[],[],[]]'),
('[[1,2,3],{"k1":"v1"},{"k2":2},[4,5,6]]'),
('[{"MySQL":"开源数据库"},"MySQL"]'),
('[{"开源数据库":"MySQL"},"开源数据库"]');

select json_array_length(json),json from test_json order by 1,2;

truncate test_json;

#
# nested function call
#

# test on table
insert into test_json values
(null),
('{"k1":[1,{"k2":["v1",2,true]},3,false]}');

select json_array_length(json_extract(json, 'k1')),json
from test_json where json_valid(json) is true order by 1,2;

truncate test_json;
