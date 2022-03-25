connection master;
eval set @@global.binlog_row_image = $binlog_row_image;
eval set @@session.binlog_row_image = $binlog_row_image;
set @@global.response_attrs_contain_binlog_row_image_delta = true;
set @@session.response_attrs_contain_binlog_row_image_delta = true;
set @@global.session_track_response_attributes = true;
set @@session.session_track_response_attributes = true;

connection slave;
source include/stop_slave.inc;
eval set @@global.binlog_row_image = $binlog_row_image;
set @@global.response_attrs_contain_binlog_row_image_delta = true;
set @@global.session_track_response_attributes = true;
source include/start_slave.inc;

connection master;

create table t1 (a varchar(10), b varchar(10), c int) engine = innodb;

insert into t1 values("1", "1", 1);
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

update t1 set a = "11";
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

update t1 set a = "1";
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

delete from t1;
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

begin;
insert into t1 values("1", "1", 1);
update t1 set a = "11";
rollback;
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

begin;
insert into t1 values("1", "1", 1);
update t1 set a = "11";
commit;
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

delete from t1;
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

begin;
insert into t1 values("1", "1", 1);
update t1 set a = "11";
delete from t1;
commit;
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

alter table t1 modify a char(10);

insert into t1 values("1", "1", 1);
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

update t1 set a = "11";
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

update t1 set a = "1";
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

delete from t1;
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

alter table t1 modify a binary(10);

insert into t1 values("1", "1", 1);
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

update t1 set a = "11";
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

update t1 set a = "1";
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

delete from t1;
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

alter table t1 modify a varbinary(10);

insert into t1 values("1", "1", 1);
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

update t1 set a = "11";
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

update t1 set a = "1";
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

delete from t1;
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

alter table t1 modify a blob;

insert into t1 values("1", "1", 1);
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

update t1 set a = "11";
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

update t1 set a = "1";
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

update t1 set a = repeat('a', 65535);
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

delete from t1;
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

alter table t1 modify a text;

insert into t1 values("1", "1", 1);
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

update t1 set a = "11";
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

update t1 set a = "1";
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

delete from t1;
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

alter table t1 modify a enum("1", "11", "111", "1111");

insert into t1 values("1", "1", 1);
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

update t1 set a = "11";
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

update t1 set a = "1";
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

delete from t1;
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

alter table t1 modify a set("1", "11", "111", "1111");

insert into t1 values("1", "1", 1);
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

update t1 set a = "11";
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

update t1 set a = "1";
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

delete from t1;
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

alter table t1 modify a json;

insert into t1 values('{"a": "b"}', '1', 1);
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

update t1 set a = json_replace(a, '$.a', 'bb');
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

update t1 set a = json_replace(a, '$.a', 'b');
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

delete from t1;
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

set @@session.binlog_row_value_options = PARTIAL_JSON;
insert into t1 values('{"a": "b"}', '1', 1);
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

update t1 set a = json_replace(a, '$.a', 'bb');
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

update t1 set a = json_replace(a, '$.a', 'b');
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

delete from t1;
let $delta = get_response_attribute(binlog_row_image_delta);
echo $delta;

set @@session.binlog_row_value_options = default;

drop table t1;

source include/sync_slave_sql_with_master.inc;

connection master;
set @@global.binlog_row_image = default;
set @@global.response_attrs_contain_binlog_row_image_delta = default;
set @@global.session_track_response_attributes = default;

connection slave;
source include/stop_slave.inc;
set @@global.binlog_row_image = default;
set @@global.response_attrs_contain_binlog_row_image_delta = default;
set @@global.session_track_response_attributes = default;
source include/start_slave.inc;
