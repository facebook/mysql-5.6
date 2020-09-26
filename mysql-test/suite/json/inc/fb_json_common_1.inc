--echo #
--echo # json_array_length
--echo #

--echo # test on table
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

select json_array_length(json) as json_array_length,json from test_json order by json_array_length,json;

truncate test_json;

--echo #
--echo # json_contains_key
--echo #

--echo # test on table
insert into test_json values
(null),
('{}'),
('{"k1":"v1"}'),
('[1,2,3,4]'),
('{"k1":{"k2":{"k3":12.345}}}'),
('{"k1":"v1","k1":{"k2":"v2"}}'),
('{"k1":[1,{"k2":["v1",2,true]},3,false]}');

select json_contains_key(json,null) as json_contains_key,json from test_json order by json_contains_key,json;
select json_contains_key(json,'k1',null) as json_contains_key,json from test_json order by json_contains_key,json;
select json_contains_key(json,'k2') as json_contains_key,json from test_json order by json_contains_key,json;
select json_contains_key(json,'k1') as json_contains_key,json from test_json order by json_contains_key,json;
select json_contains_key(json,'k1','k2') as json_contains_key,json from test_json order by json_contains_key,json;
select json_contains_key(json,'k1','k2','k3') as json_contains_key,json from test_json order by json_contains_key,json;
select json_contains_key(json,0) as json_contains_key,json from test_json order by json_contains_key,json;
select json_contains_key(json,'1') as json_contains_key,json from test_json order by json_contains_key,json;
select json_contains_key(json,'k1',1) as json_contains_key,json from test_json order by json_contains_key,json;
select json_contains_key(json,'k1',1,'k2') as json_contains_key,json from test_json order by json_contains_key,json;
select json_contains_key(json,'k1',1,'k2',0) as json_contains_key,json from test_json order by json_contains_key,json;

truncate test_json;

--echo # test multi-byte characters
insert into test_json values
('{"MySQL":"开源数据库"}'),
('{"开源数据库":"MySQL"}');

select length(json_contains_key(json,'MySQL')) as length,json_contains_key(json,'MySQL') as json_contains_key,json from test_json order by length,json_contains_key,json;
select length(json_contains_key(json,'开源数据库')) as length,json_contains_key(json,'开源数据库') as json_contains_key,json from test_json order by length,json_contains_key,json;

truncate test_json;

--echo #
--echo # json_extract
--echo #

--echo # test on table
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

--echo # test create table...select
truncate test_json;
insert into test_json values ('{"k1":[1,{"k2":["v1",2,true]},3,false]}');
create table test_json2 (json text) select json_extract(json,'k1',1,'k2',0) json from test_json;
show columns from test_json2;
select * from test_json2;
drop table test_json2;

truncate test_json;

--echo # test multi-byte characters
insert into test_json values
('{"MySQL":"开源数据库"}'),
('{"开源数据库":"MySQL"}');

select length(json_extract(json,'MySQL')),json_extract(json,'MySQL'),json from test_json order by 1,2,3;
select length(json_extract(json,'开源数据库')),json_extract(json,'开源数据库'),json from test_json order by 1,2,3;

truncate test_json;

--echo # test special case: one key is prefix of another
insert into test_json values ('{"k":1, "kk":2, "kkkk":4}');
select json_extract(json, 'k') k,
       json_extract(json, 'kk') kk,
       json_extract(json, 'kkk') kkk,
       json_extract(json, 'kkkk') kkkk
from test_json;

truncate test_json;

--echo #
--echo # nested function call
--echo #

--echo # test on table
insert into test_json values
(null),
('{"k1":[1,{"k2":["v1",2,true]},3,false]}');

select json_array_length(json_extract(json, 'k1')),json
from test_json where json_valid(json) is true order by 1,2;

truncate test_json;

--echo #
--echo # json_extract_value
--echo #

--echo # test on table
insert into test_json values
(null),
('{}'),
('{"k1":"v1"}'),
('{"k1":true}'),
('{"k1":false}'),
('{"k1":null}'),
('{"k1":12}'),
('{"k1":1.23}');
insert into test_json values(json_extract_value('{"k1":{"k2": "v2", "k2" : "v3"}, "k1":{"k2": "v4", "k2" : "v5"}}', 'k1'));

select length(json_extract_value(json,null)),json_extract_value(json,null),json from test_json order by 1,2,3;
select length(json_extract_value(json,'k1')),json_extract_value(json,'k1'),json from test_json order by 1,2,3;
select length(json_extract_value(json,'k1','k2')),json_extract_value(json,'k1','k2'),json from test_json order by 1,2,3;

--echo # json_extract_value in where clause
select json from test_json where json_extract_value(json,'k1') > 1 order by 1;

truncate test_json;

--echo #
--echo # json_contains
--echo #

--echo # test on table
insert into test_json values
('{}'),
('{"k1":"v1"}'),
('{"k1":1}'),
('[1,2,3,4]'),
('{"k1":{"k2":{"k3":12.345}}}'),
('{"k1":"v1","k1":{"k2":"v2"}}'),
('{"k1":[1,{"k2":["v1",2,true]},3,false]}'),
('{"1":"2"}'),
('{"1":2}'),
('[{"开源数据库":"MySQL"},"开源数据库"]'),
('{"k1":true, "k2":false, "k3":null}');

set session use_fb_json_functions='use_fb_json_contains=on';
select json_contains(json,'k1','v1'),json from test_json order by 1,2;
select json_contains(json,'k2','v2'),json from test_json order by 1,2;
select json_contains(json,'k1',1),json from test_json order by 1,2;
select json_contains(json,'1',2),json from test_json order by 1,2;
select json_contains(json,'k2'),json from test_json order by 1,2;
select json_contains(json,'1'),json from test_json order by 1,2;
select json_contains(json,'1','2'),json from test_json order by 1,2;
select json_contains(json,'开源数据库', 'MySQL'),json from test_json order by 1,2;
select json_contains(json,'k1',true),json from test_json order by 1,2;
select json_contains(json,'k2',false),json from test_json order by 1,2;
select json_contains(json,'k3',null),json from test_json order by 1,2;
select json_contains(json,'k2',CONVERT('{"k3":12.345}', JSON)),json from test_json order by 1,2;
select json_contains(json,'k2',CONVERT('["v_",2,true]', JSON)),json from test_json order by 1,2;
select json_contains(json,'k2',CONVERT('["%",2,true]', JSON)),json from test_json order by 1,2;
set session use_fb_json_functions='default';

truncate test_json;
