# README: This test file is shared between the test for the SUBDOC operator and
# the CONTAINS operator. For the CONTAINS test, it will read this file and 
# first replace every occurrence of SUBDOC with CONTAINS. Then, it will swap the
# left and right operands based on these parsing rules/assumptions:
#
# - An operand is either a DOCUMENT(), doc path, or doc column
# - Only lines with the word SUBDOC will be considered for the operand swap (except comments) 
#   so queries should be written in one line
# - Spaces in operands only exist inside the DOCUMENT() function
# - NOT, DOCUMENT, SUBDOC are capitalized
# 
# Debugging tip: If the contains test is failing, comment out the "--remove_file" 
#                commands in operator_document_contains.test to see the 
#                auto-generated test file for CONTAINS in the tmp folder.


# Test of SUBDOC on document types

--disable_warnings
drop table if exists t1, t2, t3;
drop view if exists v1;
--enable_warnings

SET @start_allow_DOCUMENT_type = @@global.allow_DOCUMENT_type;
SELECT @start_allow_DOCUMENT_type;

# Populate t1 with data
CREATE TABLE t1 ( a int NOT null,
       b char (10) NOT null,
       doc DOCUMENT NOT null) engine=innodb;

insert into t1 values (1,'@1','{ "id":101, "name":"Alex", "phone":6507770001, "address":{ "houseNumber":1001, "streetName":"1st", "zipcode":98761, "state":"CA" }, "intstr":"1001", "dt":"1001" }');
insert into t1 values (2,'@2','{ "id":102, "name":"Bob", "phone":6507770002, "address":{ "houseNumber":1002, "streetName":"2nd", "zipcode":98762, "state":"AZ" }, "int64":2222222220123456789, "intstr":"1002", "dt":"0123456789ABCDEFGHIGKLMNOPQRSTUVWXYZ0123456789" }');
insert into t1 values (3,'@3','{ "id":103, "name":"Charlie", "phone":6507770003, "address":{ "houseNumber":1003, "streetName":"3rd", "zipcode":98763, "state":"NY" }, "double":3.94065645841246, "dt":"2014-11-01" }');
insert into t1 values (4,'@1','{ "id":104, "name":"Don", "phone":6507770004, "address":{ "houseNumber":1004, "streetName":"4th", "zipcode":98764, "state":"WA" }, "Children":["Alex", "Bob", "Charlie"] }');
insert into t1 values (5,'@2','{ "id":105, "name":"Evan", "phone":6507770005, "address":{ "houseNumber":1005, "streetName":"5th", "zipcode":98764, "state":"WA" }, "dt":"2014-11-11 11:59:59" }');
insert into t1 values (8,'@1','{ "id":108, "address":{ "zipcode":98768 } }');
insert into t1 values (7,'@3','{ "id":104, "address":{ "zipcode":98767, "state":"NY" } }');
insert into t1 values (6,'@2','{ "id":106, "address":{ "zipcode":98766 } }');

# Populate t2 with data
CREATE TABLE t2 ( a int NOT null,
       b char (10) NOT null,
       doc DOCUMENT NOT null) engine=innodb;
insert into t2 values (8,'@1','{ "id":108, "address":{ "zipcode":98761} }');
insert into t2 values (7,'@3','{ "id":104, "address":{ "state":"NY", "zipcode":98767 } }');
insert into t2 values (6,'@2','{ "id":106, "address":{"zipcode":98766, "state":"CA"} }');
insert into t2 values (6,'@2','{ "id":110, "address":{"zipcode":98767, "state":"NY"}, "addr":{"zipcode":98767, "state":"NY"}}');

# True comparisons
SELECT DOCUMENT('{}') SUBDOC DOCUMENT('{}');
SELECT DOCUMENT('{}') SUBDOC DOCUMENT('{"k":"v"}');
SELECT DOCUMENT('{"k1": "v1"}') SUBDOC DOCUMENT('{"k1":"v1", "k2":"v2"}');
SELECT DOCUMENT('{"k2": "v2"}') SUBDOC DOCUMENT('{"k1":"v1", "k2":"v2"}');
SELECT DOCUMENT('{"k1": "v1", "k2":"v2"}') SUBDOC DOCUMENT('{"k3": [1, 2], "k2":"v2", "k1":"v1"}');
SELECT DOCUMENT('{"k1": {"foo":"bar"}}') SUBDOC DOCUMENT('{"k2": 1, "k1":{"foo":"bar"}}');
SELECT DOCUMENT('{"k1":"v%23"}') SUBDOC DOCUMENT('{"k1":"value123", "k2":"v2"}');
SELECT DOCUMENT('{"k1":"va_ue123"}') SUBDOC DOCUMENT('{"k2":"v2", "k1":"value123"}');
SELECT DOCUMENT('{"k":"\\\\%"}') SUBDOC DOCUMENT('{"k":"%"}');
SELECT DOCUMENT('{"k":"\\\\_"}') SUBDOC DOCUMENT('{"k":"_"}');
SELECT DOCUMENT('{"k":"v"}') NOT SUBDOC DOCUMENT('{}');
SELECT DOCUMENT('{}') NOT SUBDOC DOCUMENT('[]');
SELECT DOCUMENT('[]') NOT SUBDOC DOCUMENT('[]');
SELECT DOCUMENT('[1, 2, 3]') NOT SUBDOC DOCUMENT('{"k": [1, 2, 3]}');
SELECT DOCUMENT('{"k":{"k1":"v1"}}') NOT SUBDOC DOCUMENT('{"k":{}}');
SELECT DOCUMENT('{"k1":"value123"}') NOT SUBDOC DOCUMENT('{"k1":"v%23", "k2":"v2"}');
SELECT DOCUMENT('{"k1":"value123"}') NOT SUBDOC DOCUMENT('{"k1":"va_ue123", "k2":"v2"}');

# False comparisons
SELECT DOCUMENT('{"k":"v"}') SUBDOC DOCUMENT('{}');
SELECT DOCUMENT('{}') SUBDOC DOCUMENT('[]');
SELECT DOCUMENT('[]') SUBDOC DOCUMENT('[]');
SELECT DOCUMENT('[1, 2, 3]') SUBDOC DOCUMENT('{"k": [1, 2, 3]}');
SELECT DOCUMENT('{"k":{"k1":"v1"}}') SUBDOC DOCUMENT('{"k":{}}');
SELECT DOCUMENT('{"k1":"value123"}') SUBDOC DOCUMENT('{"k1":"v%23", "k2":"v2"}');
SELECT DOCUMENT('{"k1":"value123"}') SUBDOC DOCUMENT('{"k1":"va_ue123", "k2":"v2"}');
SELECT DOCUMENT('{}') NOT SUBDOC DOCUMENT('{}');
SELECT DOCUMENT('{}') NOT SUBDOC DOCUMENT('{"k":"v"}');
SELECT DOCUMENT('{"k1": "v1"}') NOT SUBDOC DOCUMENT('{"k1":"v1", "k2":"v2"}');
SELECT DOCUMENT('{"k2": "v2"}') NOT SUBDOC DOCUMENT('{"k1":"v1", "k2":"v2"}');
SELECT DOCUMENT('{"k1": "v1", "k2":"v2"}') NOT SUBDOC DOCUMENT('{"k3": [1, 2], "k2":"v2", "k1":"v1"}');
SELECT DOCUMENT('{"k1": {"foo":"bar"}}') NOT SUBDOC DOCUMENT('{"k2": 1, "k1":{"foo":"bar"}}');
SELECT DOCUMENT('{"k1":"v%23"}') NOT SUBDOC DOCUMENT('{"k1":"value123", "k2":"v2"}');
SELECT DOCUMENT('{"k1":"va_ue123"}') NOT SUBDOC DOCUMENT('{"k2":"v2", "k1":"value123"}');
SELECT DOCUMENT('{"k":"\\\\%"}') NOT SUBDOC DOCUMENT('{"k":"%"}');
SELECT DOCUMENT('{"k":"\\\\_"}') NOT SUBDOC DOCUMENT('{"k":"_"}');

# Invalid comparisons (NOT DOCUMENT types)
--error ER_WRONG_ARGUMENTS
SELECT DOCUMENT('{}') SUBDOC '{}';
--error ER_WRONG_ARGUMENTS
SELECT '{}' SUBDOC DOCUMENT('{}');
--error ER_WRONG_ARGUMENTS
SELECT DOCUMENT('{}') SUBDOC 1;
--error ER_WRONG_ARGUMENTS
SELECT 1 SUBDOC DOCUMENT('{}');
--error ER_WRONG_ARGUMENTS
SELECT doc SUBDOC '{}' FROM t1;
--error ER_WRONG_ARGUMENTS
SELECT '{}' SUBDOC doc FROM t1;
--error ER_WRONG_ARGUMENTS
SELECT doc SUBDOC 1 FROM t1;
--error ER_WRONG_ARGUMENTS
SELECT 1 SUBDOC doc FROM t1;
--error ER_WRONG_ARGUMENTS
SELECT doc.address SUBDOC '{}' FROM t1;
--error ER_WRONG_ARGUMENTS
SELECT '{}' SUBDOC doc.address FROM t1;
--error ER_WRONG_ARGUMENTS
SELECT doc.address SUBDOC 1 FROM t1;
--error ER_WRONG_ARGUMENTS
SELECT 1 SUBDOC doc.address FROM t1;

# Test doc columns, doc paths, DOCUMENT() as left/right operand of comparison
SELECT DOCUMENT('{}') SUBDOC DOCUMENT('{"k":"v"}');
SELECT DOCUMENT('{"state":"NY"}') SUBDOC doc.address FROM t2;
SELECT DOCUMENT('{"id":108}') SUBDOC doc FROM t2;
SELECT doc.address SUBDOC DOCUMENT('{"zipcode":98761, "k":"v"}') FROM t2;
SELECT doc.address SUBDOC doc.addr FROM t2;
SELECT doc.address SUBDOC doc FROM t2;
SELECT doc SUBDOC DOCUMENT('{ "id":108, "address":{ "zipcode":98761}, "k":"v"}') FROM t2;
SELECT doc SUBDOC doc.address FROM t2;
SELECT doc SUBDOC doc FROM t2;

# Test combinations of doc column, doc paths, DOCUMENT() in WHERE clause
SELECT doc.id FROM t2 WHERE DOCUMENT('{}') SUBDOC DOCUMENT('{"k":"v"}');
SELECT doc.id FROM t2 WHERE DOCUMENT('{"state":"NY"}') SUBDOC doc.address;
SELECT doc.id FROM t2 WHERE DOCUMENT('{"id":108}') SUBDOC doc;
SELECT doc.id FROM t2 WHERE doc.address SUBDOC DOCUMENT('{"zipcode":98761, "k":"v"}');
SELECT doc.id FROM t2 WHERE doc.address SUBDOC doc.addr;
SELECT doc.id FROM t2 WHERE doc.address SUBDOC doc;
SELECT doc.id FROM t2 WHERE doc SUBDOC DOCUMENT('{ "id":108, "address":{ "zipcode":98761}, "k":"v"}');
SELECT doc.id FROM t2 WHERE doc SUBDOC doc.address;
SELECT doc.id FROM t2 WHERE doc SUBDOC doc;
SELECT b FROM t1 WHERE DOCUMENT('{"state": "WA"}') SUBDOC doc.address;
SELECT doc.name FROM t1 WHERE DOCUMENT('{"name": "Evan"}') NOT SUBDOC doc;

# Test combinations of doc column, doc paths, DOCUMENT() in HAVING clause
SELECT doc.id FROM t2 HAVING DOCUMENT('{}') SUBDOC DOCUMENT('{"k":"v"}');
SELECT doc.address FROM t2 HAVING DOCUMENT('{"state":"NY"}') SUBDOC doc.address;
SELECT doc FROM t2 HAVING DOCUMENT('{"id":108}') SUBDOC doc;
SELECT doc.address FROM t2 HAVING doc.address SUBDOC DOCUMENT('{"zipcode":98761, "k":"v"}');
SELECT doc.address, doc.addr FROM t2 HAVING doc.address SUBDOC doc.addr;
SELECT doc, doc.address FROM t2 HAVING doc.address SUBDOC doc;
SELECT doc FROM t2 HAVING doc SUBDOC DOCUMENT('{ "id":108, "address":{ "zipcode":98761}, "k":"v"}');
SELECT doc, doc.address FROM t2 HAVING doc SUBDOC doc.address;
SELECT doc FROM t2 HAVING doc SUBDOC doc;
SELECT doc.address FROM t1 HAVING DOCUMENT('{"state": "WA"}') SUBDOC doc.address;
SELECT doc FROM t1 HAVING DOCUMENT('{"name": "Evan"}') NOT SUBDOC doc;

# Test SUBDOC with JOIN
SELECT t1.doc.id, t2.doc.id FROM t1 JOIN t2 ON DOCUMENT('{}') SUBDOC DOCUMENT('{"k":"v"}');
SELECT t1.doc.id, t2.doc.id FROM t1 JOIN t2 ON DOCUMENT('{"state":"NY"}') SUBDOC t2.doc.address;
SELECT t1.doc.id, t2.doc.id FROM t1 JOIN t2 ON DOCUMENT('{"id":108}') SUBDOC t2.doc;
SELECT t1.doc.id, t2.doc.id FROM t1 JOIN t2 ON t2.doc.address SUBDOC DOCUMENT('{"zipcode":98761, "k":"v"}');
SELECT t1.doc.id, t2.doc.id FROM t1 JOIN t2 ON t1.doc.address SUBDOC t2.doc.addr;
SELECT t1.doc.id, t2.doc.id FROM t1 JOIN t2 ON t1.doc.address SUBDOC t2.doc;
SELECT t1.doc.id, t2.doc.id FROM t1 JOIN t2 ON t2.doc SUBDOC DOCUMENT('{ "id":108, "address":{ "zipcode":98761}, "k":"v"}');
SELECT t1.doc.id, t2.doc.id FROM t1 JOIN t2 ON t1.doc SUBDOC t2.doc.address;
SELECT t1.doc.id, t2.doc.id FROM t1 JOIN t2 ON t1.doc SUBDOC t2.doc;
SELECT t1.doc.id, t2.doc.id FROM t1 JOIN t2 ON t2.doc.address SUBDOC t1.doc.address;
SELECT t1.doc.id, t2.doc.id FROM t1 JOIN t2 ON t2.doc.address NOT SUBDOC t1.doc.address;

# Test SUBDOC with subqueries
SELECT doc.id FROM (SELECT * FROM t2 WHERE DOCUMENT('{}') SUBDOC DOCUMENT('{"k":"v"}')) temp;
SELECT doc.id FROM (SELECT * FROM t2 WHERE DOCUMENT('{"state":"NY"}') SUBDOC doc.address) temp;
SELECT doc.id FROM (SELECT * FROM t2 WHERE DOCUMENT('{"id":108}') SUBDOC doc) temp;
SELECT doc.id FROM (SELECT * FROM t2 WHERE doc.address SUBDOC DOCUMENT('{"zipcode":98761, "k":"v"}')) temp;
SELECT doc.id FROM (SELECT * FROM t2 WHERE doc.address SUBDOC doc.addr) temp;
SELECT doc.id FROM (SELECT * FROM t2 WHERE doc.address SUBDOC doc) temp;
SELECT doc.id FROM (SELECT * FROM t2 WHERE doc SUBDOC DOCUMENT('{ "id":108, "address":{ "zipcode":98761}, "k":"v"}')) temp;
SELECT doc.id FROM (SELECT * FROM t2 WHERE doc SUBDOC doc.address) temp;
SELECT doc.id FROM (SELECT * FROM t2 WHERE doc SUBDOC doc) temp;
SELECT temp.doc.id FROM (SELECT * FROM t2 WHERE doc.address SUBDOC DOCUMENT('{"zipcode":98761, "foo":"bar"}')) temp;
SELECT temp.doc.id FROM (SELECT * FROM t2 WHERE doc.address NOT SUBDOC DOCUMENT('{"zipcode":98761, "foo":"bar"}')) temp;

# Test SUBDOC with views
CREATE VIEW v1 AS (SELECT * FROM t2 WHERE doc.address SUBDOC DOCUMENT('{"zipcode":98761, "foo":"bar"}'));
SELECT * from v1;
DROP VIEW v1;

CREATE VIEW v1 AS (SELECT * FROM t2 WHERE doc.address NOT SUBDOC DOCUMENT('{"zipcode":98761, "foo":"bar"}'));
SELECT * from v1;
DROP VIEW v1;

# Populate t3 with data
CREATE TABLE t3 ( a int NOT null,
       b char (10) NOT null,
       doc DOCUMENT NOT null) engine=innodb;
insert into t3 values (1,'@1','{ "id":101, "addr":{"state":"California", "zipcode":94306}}');
insert into t3 values (2,'@2','{ "id":102, "addr":{"symbol":"%", "k":"v"}}');
insert into t3 values (3,'@3','{ "id":103, "addr":{"symbol":"_", "k":"v"}}');
insert into t3 values (4,'@4','{ "id":104, "k1":{"k3":"v3", "k2": ["wildcard", "array"]}}');

# Test SUBDOC with regex wildcards
SELECT doc.id FROM t3 WHERE DOCUMENT('{"addr":{"state":"Ca%a", "zipcode":94306}}') SUBDOC doc;
SELECT doc.id FROM t3 WHERE DOCUMENT('{"id":101, "addr":{"state":"Ca%ni_", "zipcode":94306}}') SUBDOC doc;
SELECT doc.id FROM t3 WHERE DOCUMENT('{"state":"C%nia"}') SUBDOC doc.addr;
SELECT doc.id FROM t3 WHERE DOCUMENT('{"state":"C_lifornia"}') SUBDOC doc.addr;
SELECT doc.id FROM t3 WHERE DOCUMENT('{"k2": ["wi%rd", "arr_y"] }') SUBDOC doc.k1;
SELECT doc.id FROM t3 WHERE DOCUMENT('{"symbol":"\\\\%"}') SUBDOC doc.addr;
SELECT doc.id FROM t3 WHERE DOCUMENT('{"symbol":"\\\\_"}') SUBDOC doc.addr;
SELECT doc.id FROM t3 WHERE DOCUMENT('{"i_":101, "addr":{"state":"Ca%ni_"}}') SUBDOC doc;
SELECT doc.id FROM t3 WHERE DOCUMENT('{"s%te":"C_lifornia"}') SUBDOC doc.addr;

drop table t1, t2, t3;
exit;
