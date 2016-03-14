# This inc script creates two procedures -- save_read_stats() and
# get_read_stats(). get_read_stats() prints differential rocksdb_rows_read(rr),
# rocksdb_rows_updated(ru), and rocksdb_rows_deleted(rd) values since calling
# save_read_stats().

delimiter //;
create procedure save_read_stats()
begin
  select variable_value into @rr from information_schema.global_status
    where variable_name='rocksdb_rows_read';
  select variable_value into @ru from information_schema.global_status
    where variable_name='rocksdb_rows_updated';
  select variable_value into @rd from information_schema.global_status
    where variable_name='rocksdb_rows_deleted';
end//

create procedure get_read_stats()
begin
  select variable_value - @rr as rr from information_schema.global_status
    where variable_name='rocksdb_rows_read';
  select variable_value - @ru as ru from information_schema.global_status
    where variable_name='rocksdb_rows_updated';
  select variable_value - @rd as rd from information_schema.global_status
    where variable_name='rocksdb_rows_deleted';
end//
delimiter ;//

