set -e

NUMTABLES=100
for ((i=0; i<$NUMTABLES; i++));
do
    stmt='create table db1.t_'$i' (i int primary key) engine=innodb';
    stmt=$stmt'; insert into db1.t_'$i' values (0), (1), (2), (3), (4)';
    $MYSQL --defaults-group-suffix=.1 -e "$stmt"
done
