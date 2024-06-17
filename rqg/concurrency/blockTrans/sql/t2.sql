system sleep 1
begin;
update x set value=1000 where id=2;
system sleep 3
commit;
select * from x;

