set -e

NUMTABLES=100
socket=$1
for ((i=0; i<$NUMTABLES; i++));
do
    stmt='create table db1.t_'$i' (i int primary key) engine=innodb';
    stmt=$stmt'; insert into db1.t_'$i' values (0), (1), (2), (3), (4)';
    mysql --user=root --socket=$socket -e "$stmt"
done
