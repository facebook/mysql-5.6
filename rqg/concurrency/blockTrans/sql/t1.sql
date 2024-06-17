begin;
update x set value=value+1 where id=1;
system date
system sleep 2
system date
update x set value=value+1 where id=2;
update x set value=value+1 where id=3;
system sleep 2
select * from x;
commit;

