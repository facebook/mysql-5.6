set global rocksdb_lock_wait_timeout=1;
drop table if exists x;
create table x (id int primary key, value int) engine=RocksDB;
insert into x values (1,0), (2,0), (3,0);

