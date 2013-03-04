. inc/common.sh

function race_create_drop()
{
  vlog "Started create/drop table cycle"
  race_cycle_num=$1
  if [ -z $race_cycle_num ]
  then
    race_cycle_num=1000
  fi
  run_cmd ${MYSQL} ${MYSQL_ARGS} -e "create database race;"
  race_cycle_cnt=0;
  while [ "$race_cycle_num" -gt "$race_cycle_cnt"]
  do
	t1=tr$RANDOM
	t2=tr$RANDOM
	t3=tr$RANDOM
	${MYSQL} ${MYSQL_ARGS} -e "create table $t1 (a int) ENGINE=InnoDB;" race
	${MYSQL} ${MYSQL_ARGS} -e "create table $t2 (a int) ENGINE=InnoDB;" race
	${MYSQL} ${MYSQL_ARGS} -e "create table $t3 (a int) ENGINE=InnoDB;" race
	${MYSQL} ${MYSQL_ARGS} -e "drop table $t1;" race
	${MYSQL} ${MYSQL_ARGS} -e "drop table $t2;" race
	${MYSQL} ${MYSQL_ARGS} -e "drop table $t3;" race
	let "race_cycle_cnt=race_cycle_cnt+1"
  done
}

start_server --innodb_file_per_table

run_cmd race_create_drop &

sleep 3
# Full backup
mkdir -p $topdir/data/full
vlog "Starting backup"
xtrabackup --datadir=$mysql_datadir --backup --target-dir=$topdir/data/full
vlog "Full backup done"
