import hashlib
import MySQLdb
import os
import random
import signal
import sys
import threading
import time

table_ddl = {
't1' : """
CREATE TABLE t1(id INT AUTO_INCREMENT PRIMARY KEY,
                msg VARCHAR(255),
                KEY msg_i(msg))
ENGINE=INNODB
ROW_FORMAT=COMPRESSED
KEY_BLOCK_SIZE=1
""",
't2' : """
CREATE TABLE t2(id INT AUTO_INCREMENT PRIMARY KEY,
                msg VARCHAR(255),
                KEY msg_i(msg))
ENGINE=INNODB
""",
't3' : """
CREATE TABLE t3(id INT AUTO_INCREMENT PRIMARY KEY,
                msg text,
                KEY msg_i(msg(250)))
ENGINE=INNODB
ROW_FORMAT=COMPRESSED
KEY_BLOCK_SIZE=1
""",
't4' : """
CREATE TABLE t4(id INT AUTO_INCREMENT PRIMARY KEY,
                msg text,
                KEY msg_i(msg(250)))
ENGINE=INNODB
"""}

def fill_table(con, table_name, count, rng, log):
  cur = con.cursor()

  if table_name in ['t3', 't4']:
    max_len = 9000
  else:
    max_len = 250

  for i in range(count):
    cur.execute("""
INSERT INTO %s(id,msg) VALUES (NULL, LPAD(%d, %d, 'x'))
""" % (table_name, rng.randint(1,100000), rng.randint(1, max_len)))

  print("inserted %d rows to %s" % (count, table_name), file=log)
  cur.execute("select count(*) from %s" % table_name)
  row = cur.fetchone()
  print("...then read %d rows of %d" % (row[0], count), file=log)
  cur.close()

def create_table(con, table_name, log):
  assert table_name in table_ddl
  print("creating %s with %s" % (table_name, table_ddl[table_name]), file=log)
  cur = con.cursor()
  cur.execute(table_ddl[table_name])
  print("created %s" % table_name, file=log)
  cur.close()

def create_tables(con, min_records, rng, log):
  for table_name in tables:
    create_table(con, table_name, log)
    fill_table(con, table_name, min_records, rng, log)

def drop_tables(con, log):
  for table_name in tables:
    try:
      cur = con.cursor()
      cur.execute("drop table %s" % table_name)
      cur.close()
    except MySQLdb.Error as e:
      print("drop_tables: mysql error for %s, %s" % (table_name, e), file=log)

class Dropper(threading.Thread):
  global LG_TMP_DIR

  def __init__(self, con, min_records, max_records, rng):
    threading.Thread.__init__(self)
    con.autocommit(True)
    self.con = con
    self.min_records = min_records
    self.max_records = max_records
    self.log = open('/%s/dropper.log' % LG_TMP_DIR, 'a')
    self.rng = rng
    self.num_drops = 0

  def run(self):
    self.start_time = time.time()
    try:
      print("dropper started", file=self.log)
      self.runme()
    except Exception as e:
      print("caught (%s)" % e, file=self.log)
    finally:
      self.finish()

  def runme(self):
    while not shutdown_now:
      time.sleep(1)
      stmt = None
      try:
        for table_name in tables:
          cur = self.con.cursor()
          stmt = "select count(*) from %s" % table_name
          cur.execute(stmt)
          row = cur.fetchone()
          cur.close()
          print("Read %d rows from %s" % (row[0], table_name), file=self.log)

          if row[0] >= self.max_records:
            cur = self.con.cursor()
            stmt = "drop table %s" % table_name
            cur.execute(stmt)
            cur.close()
            print("dropped %s" % table_name, file=self.log)

            done = False
            loop = 0
            stmt = "creating %s" % table_name
            while not done and loop < 1000:
              try:
                loop += 1
                create_table(self.con, table_name, self.log)
                done = True
              except MySQLdb.Error as e:
                print("mysql error for create %s, loop %d, %s" % (table_name, loop, e), file=self.log)

            stmt = "filling %s" % table_name
            fill_table(self.con, table_name, self.min_records, self.rng, self.log)
            self.num_drops += 1
      except MySQLdb.Error as e:
        print("mysql error for stmt(%s) %s" % (stmt, e), file=self.log)

  def finish(self):
    print("total time: %.2f s" % (time.time() - self.start_time), file=self.log)
    print("dropped %d" % self.num_drops, file=self.log)
    self.log.close()

class Worker(threading.Thread):
  global LG_TMP_DIR

  def __init__(self, xid, con, server_pid):
    threading.Thread.__init__(self)
    self.xid = xid
    con.autocommit(True)
    self.con = con
    cur = self.con.cursor()
    self.rng = random.Random()
    self.rng.seed(xid * server_pid)
    self.loop_num = 0
    self.num_inserts = 0
    self.num_deletes = 0
    self.num_updates = 0
    self.log = open('/%s/worker%02d.log' % (LG_TMP_DIR, self.xid), 'a')

  def finish(self):
    print("loop_num:%d, total time: %.2f s" % (
        self.loop_num, time.time() - self.start_time), file=self.log)
    print("%d inserts, %d deletes, %d updates" % (
        self.num_inserts, self.num_deletes, self.num_updates), file=self.log)
    self.log.close()

  def run(self):
    try:
      self.runme()
    except Exception as e:
      print("caught (%s)" % e, file=self.log)
    finally:
      self.finish()

  def runme(self):
    self.start_time = time.time()
    print("thread %d started" % self.xid, file=self.log)

    while not shutdown_now:
      table_name = self.rng.choice(tables)
      self.loop_num += 1
      try:
        stmt = None

        cur = self.con.cursor()
        cur.execute("select count(*) from %s" % table_name)
        row = cur.fetchone()
        num_rows = row[0]

        if not num_rows:
          time.sleep(1)
          cur.close()
          print("sleep after 0 rows found in %s" % table_name, file=self.log)
          continue

        if table_name in ['t3', 't4']:
          max_len = 9000
        else:
          max_len = 240

        msg = str(rng.randint(1,100000)) + ('z' * rng.randint(1,max_len))
        idx = self.rng.randint(1, num_rows)

        operation = self.rng.randint(1, 100)
        if operation < 70:
          stmt = "INSERT INTO %s (msg,id) VALUES ('%s', NULL)" % (table_name, msg)
          self.num_inserts += 1

        elif operation < 95:
          stmt = "UPDATE %s SET msg='%s' WHERE id=%d" % (table_name, msg, idx)
          self.num_updates += 1

        else:
          stmt = "DELETE FROM %s WHERE id=%d" % (table_name, idx)
          self.num_deletes += 1

        cur.execute(stmt)
        cur.close()

      except MySQLdb.Error as e:
        print("mysql error for stmt(%s) %s" % (stmt, e), file=self.log)

if  __name__ == '__main__':
  global LG_TMP_DIR
  global shutdown_now
  global tables

  shutdown_now = False
  pid_file = sys.argv[1]
  LG_TMP_DIR = sys.argv[2]
  min_records = int(sys.argv[3])
  max_records = int(sys.argv[4])
  num_workers = int(sys.argv[5])
  test_seconds = int(sys.argv[6])
  use_blob = int(sys.argv[7])
  user = sys.argv[8]
  host = sys.argv[9]
  port = int(sys.argv[10])
  db = 'test'
  workers = []
  server_pid = int(open(pid_file).read())
  log = open('/%s/main.log' % LG_TMP_DIR, 'a')

  if use_blob:
    tables = ['t1', 't2', 't3', 't4']
  else:
    tables = ['t1', 't2']

  rng = random.Random()
  rng.seed(server_pid)

  con = MySQLdb.connect(user=user, host=host, port=port, db=db)
  con.autocommit(True)
  create_tables(con, min_records, rng, log)
  con.close()
  log.flush()

  print("start %d threads" % num_workers, file=log)
  for i in range(num_workers):
    worker = Worker(i,
                    MySQLdb.connect(user=user, host=host, port=port, db=db),
                    server_pid)
    worker.start()
    workers.append(worker)

  log.flush()

  dropper = Dropper(MySQLdb.connect(user=user, host=host, port=port, db=db),
                    min_records, max_records, rng)
  dropper.start()

  print("wait for %d seconds" % test_seconds, file=log)
  time.sleep(test_seconds)
  shutdown_now = True
  dropper.join()

  print("wait for threads", file=log)
  for w in workers:
    w.join()

  print("all threads done", file=log)

  con = MySQLdb.connect(user=user, host=host, port=port, db=db)
  con.autocommit(True)
  drop_tables(con, log)
  con.close()
  log.close()
