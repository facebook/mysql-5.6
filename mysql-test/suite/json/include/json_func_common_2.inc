insert into test_json values ('');

# invalid json object (expecting error)
--error ER_INVALID_JSON
select json_extract(json, 'k1') from test_json;

truncate test_json;

insert into test_json values ('');

# invalid json object (expecting error)
--error ER_INVALID_JSON
select json_contains_key(json, 'k1') from test_json;

truncate test_json;

insert into test_json values ('');

# invalid json object (expecting error)
--error ER_INVALID_JSON
select json_array_length(json) from test_json;

truncate test_json;

#
# cleanup
#
drop table test_json;
