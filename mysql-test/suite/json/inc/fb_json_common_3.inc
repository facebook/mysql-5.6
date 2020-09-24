--echo #
--echo # json_extract compare semantics
--echo #
insert into test_json values
('Hello'),
('"Hello"'),
(123),
('123'),
('"123"'),
(true),
('true'),
('"true"'),
(false),
('false'),
('"false"'),
(null),
('null'),
('"null"');

select json from test_json where json_extract('{"k1":"Hello"}', 'k1') = json order by json;
select json from test_json where json_extract('{"k1":123}', 'k1') = json order by json;
select json from test_json where json_extract('{"k1":"123"}', 'k1') = json order by json;
select json from test_json where json_extract('{"k1":true}', 'k1') = json order by json;
select json from test_json where json_extract('{"k1":"true"}', 'k1') = json order by json;
select json from test_json where json_extract('{"k1":false}', 'k1') = json order by json;
select json from test_json where json_extract('{"k1":"false"}', 'k1') = json order by json;
select json from test_json where json_extract('{"k1":null}', 'k1') = json order by json;
select json from test_json where json_extract('{"k1":"null"}', 'k1') = json order by json;
select json from test_json where json = json_extract('{"k1":"Hello"}', 'k1') order by json;
select json from test_json where json = json_extract('{"k1":123}', 'k1') order by json;
select json from test_json where json = json_extract('{"k1":"123"}', 'k1') order by json;
select json from test_json where json = json_extract('{"k1":true}', 'k1') order by json;
select json from test_json where json = json_extract('{"k1":"true"}', 'k1') order by json;
select json from test_json where json = json_extract('{"k1":false}', 'k1') order by json;
select json from test_json where json = json_extract('{"k1":"false"}', 'k1') order by json;
select json from test_json where json = json_extract('{"k1":null}', 'k1') order by json;
select json from test_json where json = json_extract('{"k1":"null"}', 'k1') order by json;

truncate test_json;

--echo #
--echo # json_extract_value compare semantics
--echo #
insert into test_json values
('Hello'),
('"Hello"'),
(123),
('123'),
('"123"'),
(1),
('1'),
('"1"'),
(0),
('0'),
('"0"'),
(true),
('true'),
('"true"'),
(false),
('false'),
('"false"'),
(null),
('null'),
('"null"');

select json from test_json where json_extract_value('{"k1":"Hello"}', 'k1') = json order by json;
select json from test_json where json_extract_value('{"k1":123}', 'k1') = json order by json;
select json from test_json where json_extract_value('{"k1":"123"}', 'k1') = json order by json;
select json from test_json where json_extract_value('{"k1":true}', 'k1') = json order by json;
select json from test_json where json_extract_value('{"k1":"true"}', 'k1') = json order by json;
select json from test_json where json_extract_value('{"k1":false}', 'k1') = json order by json;
select json from test_json where json_extract_value('{"k1":"false"}', 'k1') = json order by json;
select json from test_json where json_extract_value('{"k1":null}', 'k1') = json order by json;
select json from test_json where json_extract_value('{"k1":"null"}', 'k1') = json order by json;
select json from test_json where json_extract_value('{"k1":1}', 'k1') = json order by json;
select json from test_json where json_extract_value('{"k1":"1"}', 'k1') = json order by json;
select json from test_json where json_extract_value('{"k1":0}', 'k1') = json order by json;
select json from test_json where json_extract_value('{"k1":"0"}', 'k1') = json order by json;

truncate test_json;
