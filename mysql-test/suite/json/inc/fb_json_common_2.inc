insert into test_json values ('');

# invalid json object (expecting error)
set session use_fb_json_functions = "use_fb_json_extract=on";
--error ER_INVALID_JSON_TEXT_IN_PARAM
select json_extract(json, 'k1') from test_json;
set session use_fb_json_functions = default;

truncate test_json;

insert into test_json values ('');

# invalid json object (expecting error)
--error ER_INVALID_JSON_TEXT_IN_PARAM
select json_contains_key(json, 'k1') from test_json;

truncate test_json;

insert into test_json values ('');

# invalid json object (expecting error)
--error ER_INVALID_JSON_TEXT_IN_PARAM
select json_array_length(json) from test_json;

truncate test_json;

insert into test_json values ('');

# invalid json object (expecting error)

set session use_fb_json_functions='use_fb_json_contains=on';
--error ER_INVALID_JSON_TEXT_IN_PARAM
select json_contains(json, 'k1') from test_json;
--error ER_INVALID_JSON_TEXT_IN_PARAM
select json_contains(json, 'k1', 'v1') from test_json;
set session use_fb_json_functions='default';

truncate test_json;
