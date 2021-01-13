"""
This script stress tests partial indexes by performing writes while concurrently checking PK/SK consistency.

Usage: partial_index_stress.py user host port db_name table_name
       num_iters num_threads
"""
import MySQLdb
import random
import sys
import threading
import traceback

def get_query(table_name, binary_id1):
  link_type = random.randint(1, 2)
  id1 = random.randint(1, 5)
  id2 = random.randint(1, 20)

  r = random.randint(1, 3)

  if r == 1:
    if binary_id1:
      return """DELETE FROM %s WHERE id1 = "%d" and id2 = %d and link_type = %d""" % (table_name, id1, id2, link_type)
    else:
      return """DELETE FROM %s WHERE id1 = %d and id2 = %d and link_type = %d""" % (table_name, id1, id2, link_type)
  else:
    return """INSERT INTO %s VALUES (%d, 0, %d, 0, %d, 1, 'abc', 100, 1) ON DUPLICATE KEY UPDATE time=time+10, version=version+1""" % (table_name, id1, id2, link_type)

class Worker(threading.Thread):
  def __init__(self, con, table_name, num_iters, check, event):
    threading.Thread.__init__(self)
    self.con = con
    self.table_name = table_name
    self.num_iters = num_iters
    self.check = check
    self.event = event
    self.exception = None
    self.start()

  def run(self):
    try:
      if self.check:
        self.run_check()
      else:
        self.run_write()
    except Exception as e:
      self.exception = traceback.format_exc()

  def run_write(self):
    cur = self.con.cursor()
    cur.execute("select data_type = 'binary' from information_schema.columns where table_schema = database() and table_name = '%s' and column_name = 'id1'" % self.table_name);
    binary_id1 = cur.fetchone()[0] == 1
    cur.execute("SET SESSION TRANSACTION ISOLATION LEVEL READ COMMITTED")
    for x in range(self.num_iters):
      try:
        cur.execute(get_query(self.table_name, binary_id1))
        self.con.commit()
      except MySQLdb.OperationalError as e:
        self.con.rollback()
        cur = self.con.cursor()
        raise e

  def run_check(self):
    cur = self.con.cursor()
    while not self.event.is_set():
      try:
        cur.execute("SELECT COUNT(*) FROM %s FORCE INDEX(PRIMARY) UNION ALL SELECT COUNT(*) FROM %s FORCE INDEX(id1_type)" % (self.table_name, self.table_name))
        pk_count = cur.fetchone()[0]
        sk_count = cur.fetchone()[0]
        assert pk_count == sk_count, "Count mismatch %d != %d" % (pk_count, sk_count)
        self.con.commit()
      except MySQLdb.OperationalError as e:
        self.con.rollback()
        cur = self.con.cursor()
        raise e

if __name__ == '__main__':
  if len(sys.argv) != 8:
    print("Usage: partial_index_stress.py user host port db_name " \
          "table_name num_iters num_threads")
    sys.exit(1)

  user = sys.argv[1]
  host = sys.argv[2]
  port = int(sys.argv[3])
  db = sys.argv[4]
  table_name = sys.argv[5]
  num_iters = int(sys.argv[6])
  num_workers = int(sys.argv[7])

  done_event = threading.Event();

  worker_failed = False
  workers = []
  for i in range(num_workers):
    w = Worker(
      MySQLdb.connect(user=user, host=host, port=port, db=db), table_name,
      num_iters, False, None)
    workers.append(w)

  checker = Worker(
    MySQLdb.connect(user=user, host=host, port=port, db=db), table_name,
    num_iters, True, done_event)

  for w in workers:
    w.join()
    if w.exception:
      print("Worker hit an exception:\n%s\n" % w.exception)
      worker_failed = True

  done_event.set()
  checker.join()

  if worker_failed:
    sys.exit(1)
