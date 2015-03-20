# Description
# -----------
# Test cases for FBSON input

show variables like 'use_fbson_input_format';

# not a global var
--error ER_LOCAL_VARIABLE
set global use_fbson_input_format = true;

create table test_json2(json document) engine=innodb;

set use_fbson_input_format = true;
insert into test_json2 values
(fbson(null)),
(fbson('{}')),
(fbson('{"k1":"v1"}')),
(fbson('{"k1":true}')),
(fbson('{"k1":false}')),
(fbson('{"k1":null}')),
(fbson('{"k1":12}')),
(fbson('{"k1":1.23}')),
(fbson('[1,2,3,4]')),
(fbson('{"k1":{"k2":{"k3":12.345}}}')),
(fbson('{"k1":"v1","k1":{"k2":"v2"}}')),
(fbson('{"k1":[1,{"k2":["v1",2,true]},3,false]}')),
(fbson('{"MySQL":"开源数据库"}')),
(fbson('{"开源数据库":"MySQL"}'));
set use_fbson_input_format = false;

select * from test_json2;

drop table test_json2;
