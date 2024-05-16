# prepare valid and invalid values for boolean type
disable_query_log;
CREATE TABLE valid_values (value varchar(255)) ENGINE=myisam;
INSERT INTO valid_values VALUES (1), (0), ('on'), ('off'), ('true'), ('false');

CREATE TABLE invalid_values (value varchar(255)) ENGINE=myisam;
INSERT INTO invalid_values VALUES ('\'aaa\''), ('\'bbb\'');
enable_query_log;
