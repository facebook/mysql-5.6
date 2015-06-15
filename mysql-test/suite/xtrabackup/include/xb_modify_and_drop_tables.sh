set -e

stmt='insert into db1.t_99 values (100)';
stmt='drop table db1.t_99';
$MYSQL --defaults-group-suffix=.1 -e "$stmt"
