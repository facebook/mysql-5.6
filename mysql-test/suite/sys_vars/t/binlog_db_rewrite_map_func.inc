############################################
### TEST WITH CATCHUP FOR OTHER DATABASE ###
############################################


if (!$other)
{
  --let $other=_other_
}

# Create the databases
--eval CREATE DATABASE $rewrite_from_db
--eval CREATE DATABASE $other

--eval CREATE TABLE $rewrite_from_db.test (id int);
--eval CREATE TABLE $other.t1 (a int);

##
## START OF BINLOG FILE
##

FLUSH LOGS;

--eval USE $rewrite_from_db;
INSERT INTO test (id) VALUES (1);
INSERT INTO test (id) VALUES (2), (3);

--eval USE $other;
INSERT INTO t1 (a) VALUES (3);
INSERT INTO t1 (a) VALUES (4), (5);

##
## END OF BINLOG FILE
##

--let $binlog_file= query_get_value(SHOW MASTER STATUS, File, 1)
FLUSH LOGS;
--copy_file $datadir/$binlog_file $datadir/backup_$binlog_file

--eval SELECT * FROM $rewrite_from_db.test
--eval SELECT * FROM $other.t1

##
## RESET INSTANCE (WITH NEW DB)
##

--eval DROP DATABASE $rewrite_from_db
--eval CREATE DATABASE $rewrite_to_db
--eval CREATE TABLE $rewrite_to_db.test (id int);
--eval TRUNCATE TABLE $other.t1


# Do the actual application of binlogs to different table
--exec $MYSQL_BINLOG --skip-gtids --force-if-open $datadir/backup_$binlog_file | (echo "SET SESSION binlog_db_rewrite_map='$rewrite_from_db:$rewrite_to_db';" && cat) | $MYSQL --local-infile=1


--eval SELECT * FROM $rewrite_to_db.test
--eval SELECT * FROM $other.t1


--eval DROP DATABASE $rewrite_to_db
--eval DROP DATABASE $other
--remove_file $datadir/backup_$binlog_file


###############################################
### TEST WITHOUT CATCHUP FOR OTHER DATABASE ###
###############################################


# Create the databases
--eval CREATE DATABASE $rewrite_from_db
--eval CREATE DATABASE $other

--eval CREATE TABLE $rewrite_from_db.test (id int);
--eval CREATE TABLE $other.t1 (a int);

##
## START OF BINLOG FILE
##

FLUSH LOGS;

--eval USE $rewrite_from_db;
INSERT INTO test (id) VALUES (1);
INSERT INTO test (id) VALUES (2), (3);

--eval USE $other;
INSERT INTO t1 (a) VALUES (3);
INSERT INTO t1 (a) VALUES (4), (5);

##
## END OF BINLOG FILE
##

--let $binlog_file= query_get_value(SHOW MASTER STATUS, File, 1)
FLUSH LOGS;
--copy_file $datadir/$binlog_file $datadir/backup_$binlog_file

--eval SELECT * FROM $rewrite_from_db.test
--eval SELECT * FROM $other.t1

##
## RESET INSTANCE (WITH NEW DB)
##

--eval DROP DATABASE $rewrite_from_db
--eval CREATE DATABASE $rewrite_to_db
--eval CREATE TABLE $rewrite_to_db.test (id int);
--eval TRUNCATE TABLE $other.t1


# Do the actual application of binlogs to different table
--exec $MYSQL_BINLOG --database=$rewrite_from_db --skip-gtids --force-if-open $datadir/backup_$binlog_file | (echo "SET SESSION binlog_db_rewrite_map='$rewrite_from_db:$rewrite_to_db';" && cat) | $MYSQL --local-infile=1


--eval SELECT * FROM $rewrite_to_db.test
--eval SELECT * FROM $other.t1


--eval DROP DATABASE $rewrite_to_db
--eval DROP DATABASE $other
--remove_file $datadir/backup_$binlog_file
