socket=$1
stmt='insert into db1.t_99 values (100)';
stmt='drop table db1.t_99';
mysql --user=root --socket=$socket -e "$stmt"
