# Description
# -----------
# Test cases for FBSON output

# test fbson output
show variables like 'use_fbson_output_format';

# not a global var
--error ER_LOCAL_VARIABLE
set global use_fbson_output_format = true;

set use_fbson_output_format = true;

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

select HEX(json) from test_json;

# json_extract and json_extract_value are equivalent when outputting FBSON
# since the binary is not converted to JSON in the results
select HEX(json_extract(json,null)) from test_json order by 1;
select HEX(json_extract(json,'k1')) from test_json order by 1;
select HEX(json_extract(json,'k1','k2')) from test_json order by 1;
select HEX(json_extract(json,'k1','k2','k3')) from test_json order by 1;
select HEX(json_extract(json,0)) from test_json order by 1;
select HEX(json_extract(json,'1')) from test_json order by 1;
select HEX(json_extract(json,'k1',1)) from test_json order by 1;
select HEX(json_extract(json,'k1',1,'k2')) from test_json order by 1;
select HEX(json_extract(json,'k1',1,'k2',0)) from test_json order by 1;

truncate test_json;

# test multi-byte characters
insert into test_json values
('{"MySQL":"开源数据库"}'),
('{"开源数据库":"MySQL"}');

select HEX(json) from test_json;

select HEX(json_extract(json,'MySQL')) from test_json order by 1;
select HEX(json_extract(json,'开源数据库')) from test_json order by 1;

truncate test_json;

# nested function call
insert into test_json values('{"k1":[1,{"k2":["v1",2,true]},3,false]}');

select HEX(json) from test_json;

select HEX(json_extract(json_extract(json_extract(json_extract(json, 'k1'), 1), 'k2'), 0)) from test_json;
select json_contains_key(json_extract(json, 'k1', 1), 'k2') from test_json;
select json_array_length(json_extract(json, 'k1')) from test_json;
# invalid key
select HEX(json_extract(json_extract(json, 'foo'), 'k2')) from test_json;

truncate test_json;

# test JSON text

select HEX(json_extract('{"k1":[1,{"k2":["v1",2,true]},3,false]}','k1',1,'k2'));
select json_contains_key(json_extract('{"k1":[1,{"k2":["v1",2,true]},3,false]}', 'k1', 1), 'k2');

# testing large document

INSERT INTO test_json VALUES ('{\"name\":\"Bob Thompson\",\"age\":45,\"age_string\":\"45\",\"big_age\":1234567890123,\"amount\":1.23456789,\"amount_string\":\"1.23456789\",\"true_value\":true,\"true_string\":\"true\",\"true_json\":\"true\",\"false_value\":false,\"false_string\":\"false\",\"false_json\":\"false\",\"null_value\":null,\"null_string\":\"null\",\"null_json\":\"null\",\"empty_string\":\"\",\"zero\":0,\"json_vector1\":\"[]\",\"json_vector2\":\"[1,2,3]\",\"json_vector3\":\"\\\"[]\\\"\",\"json_vector4\":\"\\\"[1,2,3]\\\"\",\"json_vector5\":\"\'[]\'\",\"json_vector6\":\"\'[1,2,3]\'\",\"json_vector7\":\"[]\",\"json_vector8\":\"[1,2,3]\",\"json_map1\":\"{}\",\"json_map2\":\"{\\\\\\\"a\\\\\\\":1,\\\\\\\"b\\\\\\\":2,\\\\\\\"c\\\\\\\":3}\",\"json_map3\":\"\\\"{}\\\"\",\"json_map4\":\"\\\"{\\\\\\\"a\\\\\\\":1,\\\\\\\"b\\\\\\\":2,\\\\\\\"c\\\\\\\":3}\\\"\",\"json_map5\":\"\'{}\'\",\"json_map6\":\"\'{\\\"a\\\":1,\\\"b\\\":2,\\\"c\\\":3}\'\",\"json_map7\":\"{}\",\"json_map8\":\"{\\\"a\\\":1,\\\"b\\\":2,\\\"c\\\":3}\",\"address\":{\"street\":\"8008 Left Ln.\",\"state\":\"CA\",\"zipcode\":90210},\"jobs\":[\"CEO\",\"Director\",\"Engineer\"],\"map_of_vectors\":{\"one\":[10,20,30],\"two\":[\"x\",\"y\"],\"three\":[],\"four\":[true,false,null,1.2345]},\"vector_of_maps\":[{\"xx\":10,\"yy\":20,\"zz\":30},{\"100\":\"x\",\"324\":\"y\"},{},{\"x1\":true,\"x2\":false,\"x3\":null,\"x4\":1.2345}]}');

select HEX(json) from test_json;

set use_fbson_output_format = false;

select json from test_json;

truncate test_json;

set use_fbson_output_format = DEFAULT;
