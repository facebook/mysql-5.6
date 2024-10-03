"""
This script stress tests exactly_at_hlc by performing writes and reads in
parallel.

Usage: exactly_at_hlc.py user host port db_name table_name
       num_iters_write num_iters_read error_log_path
"""
import MySQLdb
import random
import os
import sys
import threading
import traceback

class Worker(threading.Thread):
  def __init__(self, con, tbl, num_iters, write, event, write_hlc_dict=None):
    threading.Thread.__init__(self)
    self.con = con
    self.table_name = tbl
    self.num_iters = num_iters
    self.write = write
    self.event = event
    self.write_hlc_dict = write_hlc_dict
    self.start()

  def run(self):
    try:
      if self.write:
        self.run_write()
      else:
        self.run_read()
    except Exception as e:
      print("Worker hit an exception:\n%s" % traceback.format_exc())
      self.event.set()

  def run_write(self):
    cur = self.con.cursor()
    # commit isolation change immediately, since set session transaction doesn't
    # apply to cur trx
    for x in range(self.num_iters):
      if self.event.is_set():
        break
      try:
        cur.execute("update %s SET value=value+1 where id=1" % self.table_name)
        self.con.commit()

      except MySQLdb.OperationalError as e:
        self.con.rollback()
        cur = self.con.cursor()
        raise e

  def run_read(self):
    cur = self.con.cursor()
    for x in range(self.num_iters):
      try:
        cur.execute("SELECT value FROM %s WHERE id=1 " % self.table_name)
        rows = cur.fetchall()
        self.write_hlc_dict[x] = rows[0][0]
      except Exception as e:
        self.con.rollback()
        raise e

def verify_hlc(write_dict, log_path, num_iters_read):
  # read log file
  read_hlc = {}
  with open(log_path, "r") as f:
    lines = f.readlines()
    i = 0
    for line in lines:
      # extract everything after read_hlc:
      if "Debug read_hlc:" in line:
        hlc = line.split("read_hlc: ")[1]
        read_hlc[i] = int(hlc)
        i = (i+1) % num_iters_read

  for x in range(num_iters_read):
    if read_hlc[x] != write_dict[x]:
      print("Didn't read the expected value. Expected: %d, read: %d" % \
      (write_dict[x], read_hlc[x]))
      print("Read HLC dict: %s" % read_hlc)
      print("Data dict: %s" % write_dict)
      worker_failed = True
      break

def main():
  if len(sys.argv) != 9:
    print("Usage: exactly_at_hlc.py user host port db_name " \
          "table_name num_iters_write num_iters_read error_log_path")
    sys.exit(1)

  user = sys.argv[1]
  host = sys.argv[2]
  port = int(sys.argv[3])
  db = sys.argv[4]
  table_name = sys.argv[5]
  num_iters_write = int(sys.argv[6])
  num_iters_read = int(sys.argv[7])
  log_path = sys.argv[8]

  done_event = threading.Event()

  write_hlc_dict = {}

  worker_failed = False
  workers = []
  # write worker
  w = Worker(
    MySQLdb.connect(user=user, host=host, port=port, db=db), table_name,
    num_iters_write, True, done_event)
  workers.append(w)

  # read worker
  r = Worker(
    MySQLdb.connect(user=user, host=host, port=port, db=db), table_name,
    num_iters_read, False, done_event, write_hlc_dict)
  workers.append(r)

  for w in workers:
    w.join()

  done_event.set()

  verify_hlc(write_hlc_dict, log_path, num_iters_read)

  if worker_failed:
    sys.exit(1)

if __name__ == '__main__':
  main()
