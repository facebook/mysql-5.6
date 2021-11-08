"""
This script tests Range Locking.
Usage:

  create table t1 (
    pk bigint primary key,
    group_list varchar(128),
    parent_id bigint
  ) engine=rocksdb;

  let $exec = python3 suite/rocksdb/t/range_locking_conc_test.py \
                      root 127.0.0.1 $MASTER_MYPORT test t1 \
                      num_inserts   num_insert_threads \
                      num_group_ops num_group_threads
  exec $exec;

"""

import hashlib
import MySQLdb
import os
import random
import signal
import sys
import threading
import time
import string

MAX_PK_VAL = 1000*1000*1000

counter_n_inserted = 0
counter_n_insert_failed = 0
counter_n_groups_created = 0
counter_n_group_create_fails = 0
counter_n_groups_verified = 0
counter_n_groups_deleted = 0

class Worker(threading.Thread):
  Instance = None
  def __init__(self, con, table_name, worker_type_arg, num_inserts):
    threading.Thread.__init__(self)
    self.finished = False
    self.num_inserts = num_inserts
    con.autocommit(False)
    self.con = con
    self.rand = random.Random()
    self.exception = None
    self.table_name = table_name
    self.worker_type = worker_type_arg
    Worker.Instance = self
    self.start()

  def run(self):
    self.rand.seed(threading.get_ident())
    try:
      self.cur= self.con.cursor();
      if (self.worker_type == "insert"):
        self.run_inserts()
      if (self.worker_type == "join_group"):
        self.run_create_groups()
    except Exception as e:
      self.exception = traceback.format_exc()
      print("caught (%py)" % e)
    finally:
      self.finish()

  def run_one_insert(self):
    global counter_n_inserted
    global counter_n_insert_failed
    cur = self.cur
    pk = self.rand.randint(1, MAX_PK_VAL)
    
    do_commit = False
    cur.execute("select pk,parent_id,group_list from t1 where pk>=%s limit 1 for update", (pk,));
    row = cur.fetchone()
    if row is None:
      #print("No row found, inserting %d" % (pk+1000*1000))
      cur.execute("insert into t1 (pk) values(%s)", (pk+1000*1000,));
      do_commit = True
    else:
      if ((row[0] - pk)>10 and row[1] is None):
        #print("Row found, inserting into gap, %d" % pk)
        cur.execute("insert into t1 (pk) values(%s)", (pk,));
        do_commit = True
      else:
        #print(row)
        #print("Insert: found row[1]=" + str(row[1]))
        #print("Row found, grouped or too tight")
        do_commit = False

    if (do_commit):
      cur.execute("commit")
      counter_n_inserted += 1
    else:
      cur.execute("rollback")
      counter_n_insert_failed += 1
      
  def run_one_create_group(self):
    global counter_n_groups_created
    global counter_n_group_create_fails
    global counter_n_groups_deleted
    cur = self.cur
    pk = self.rand.randint(1, MAX_PK_VAL)
      
    n_rows = 0
    n_groups_deleted= 0
    first_pk= None
    cur.execute("select pk,parent_id,group_list from t1 where pk>=%s limit 5 for update", (pk,));
    row = cur.fetchone()
    while row is not None:
      if (first_pk is None):
        first_pk = row[0]
        group_list= str(first_pk)
      else:
        group_list = group_list + "," + str(row[0])
      last_pk = row[0]
      if row[1] is not None:
        # Found a row in a group
        break;
      if row[2] is not None:
        # Found a group leader row.
        self.delete_group(row[0], row[2])
        n_groups_deleted += 1;
        break;
      n_rows += 1
      row = cur.fetchone()

    if (n_rows == 5):
      # Ok we got 5 rows in a row and they are all standalone
      # Create a group.
      # print("Creating group %d" % first_pk)
      cur.execute("update t1 set group_list=%s where pk=%s", (group_list,first_pk,))
      cur.execute("update t1 set parent_id=%s where pk > %s and pk <=%s",
                  (first_pk,first_pk, last_pk))
      cur.execute("commit")
      counter_n_groups_created += 1
      counter_n_groups_deleted += n_groups_deleted;
    else:
      # print("Failed to join a group")
      counter_n_group_create_fails += 1
      cur.execute("rollback")
  
  # Verify and delete the group
  def delete_group(self, group_id, group_list_base):
    global counter_n_groups_verified
    cur = self.con.cursor();
    cur.execute("select pk,parent_id,group_list from t1 where pk>=%s limit 5 for update", (group_id,));
    first_pk = None
    n_rows = 0

    row = cur.fetchone()
    while row is not None:
      if (first_pk is None):
        first_pk = row[0]
        group_list= str(first_pk)
        if (first_pk != group_id): 
          self.raise_error("First row is not the group leader!");
      else:
        group_list = group_list + "," + str(row[0])
      last_pk = row[0]
      if (row[0] != first_pk and row[1] != first_pk):
        self.raise_error("Row in group has wrong parent_id (expect %s got %s)" % (first_pk, row[1]))
        break;
      if (row[0] != first_pk and row[2] is not None):
        self.raise_error("Row in group is a group leader?")
        break;
      n_rows += 1
      row = cur.fetchone()

    if (n_rows != 5):
      self.raise_error("Expected %d rows got %d" % (5, n_rows,))
    if (group_list != group_list_base):
      self.raise_error("Group contents mismatch: expected '%s' got '%s'" % (group_list_base, group_list))
    # Ok, everything seems to be in order.
    cur.execute("update t1 set parent_id=NULL, group_list=NULL where pk>=%s and pk<=%s", (group_id,last_pk,));
    counter_n_groups_verified += 1

  def raise_error(self, msg):
    print("Data corruption detected: " + msg)
    sys.exit("Failed!")

  def run_inserts(self):
    #print("Worker.run_inserts")
    for i in range(self.num_inserts):
      self.run_one_insert()

  def run_create_groups(self):
    #print("Worker.run_create_groups")
    for i in range(self.num_inserts):
      self.run_one_create_group()


  def finish(self):
    self.finished = True


if __name__ == '__main__':
  if len(sys.argv) != 10:
    print("Usage: range_locking_conc_test.py " \
           "user host port db_name table_name " \
           "num_inserts num_insert_threads" \
           "num_grp_ops num_group_threads" \
           )
    sys.exit(1)

  user = sys.argv[1]
  host = sys.argv[2]
  port = int(sys.argv[3])
  db = sys.argv[4]
  table_name = sys.argv[5]

  num_inserts = int(sys.argv[6])
  num_insert_workers = int(sys.argv[7])
  num_group_ops = int(sys.argv[8])
  num_group_workers = int(sys.argv[9])

  con= MySQLdb.connect(user=user, host=host, port=port, db=db)
  con.cursor().execute("truncate table t1")

  worker_failed = False
  workers = []
  worker_type = "insert"
  num_loops = num_inserts
  for i in range(num_insert_workers + num_group_workers):
    worker = Worker(
      MySQLdb.connect(user=user, host=host, port=port, db=db), table_name,
      worker_type, num_loops)
    workers.append(worker)
    if (i == num_insert_workers - 1):
      worker_type = "join_group"
      num_loops = num_group_ops

  for w in workers:
    w.join()
    if w.exception:
      print("Worker hit an exception:\n%s\n" % w.exception)
      worker_failed = True

  print("")
  print("rows_inserted: %d" % counter_n_inserted)
  print("rows_insert_failed: %d" % counter_n_insert_failed)
  print("groups_created:  %d" % counter_n_groups_created)
  print("groups_verified: %d" % counter_n_groups_verified)
  print("groups_deleted:  %d" % counter_n_groups_deleted)
  print("")
  print("group_create_fails: %d" % counter_n_group_create_fails)

  if worker_failed:
    sys.exit(1)


