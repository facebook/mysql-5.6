import io
import hashlib
import MySQLdb
from MySQLdb.constants import *
import os
import random
import signal
import sys
import threading
import time
import string
import traceback

CHARS = string.ascii_letters + string.digits

def sha1(x):
  # x could be either unicode str or bytes encoded in utf8mb4
  # but we do expect x to consists of only ascii characters as in CHARS
  if type(x) == str:
    # Unicode - convert to ascii
    return hashlib.sha1(x.encode('ascii')).hexdigest()
  else:
    # Bytes - convert directly as they should be in ascii
    return hashlib.sha1(x).hexdigest()

# Should be deterministic given an idx
def get_msg(do_blob, idx):
  random.seed(idx);
  if do_blob:
    blob_length = random.randint(1, 24000)
  else:
    blob_length = random.randint(1, 255)

  if random.randint(1, 2) == 1:
    # blob that cannot be compressed (well, compresses to 85% of original size)
    return ''.join([random.choice(CHARS) for x in range(blob_length)])
  else:
    # blob that can be compressed
    return random.choice(CHARS) * blob_length

def is_connection_error(exc):
  error_code = exc.args[0]
  return (error_code == MySQLdb.constants.CR.CONNECTION_ERROR or
          error_code == MySQLdb.constants.CR.CONN_HOST_ERROR or
          error_code == MySQLdb.constants.CR.SERVER_LOST or
          error_code == MySQLdb.constants.CR.SERVER_GONE_ERROR or
          error_code == MySQLdb.constants.ER.QUERY_INTERRUPTED or
          error_code == MySQLdb.constants.ER.SERVER_SHUTDOWN)

class ValidateError(Exception):
  """Raised when validate_msg fails."""
  pass

class ChecksumError(Exception):
  """Raised when the ChecksumWorker finds a discrepancy."""
  pass

# Base class for worker threads
class WorkerThread(threading.Thread):
  global LG_TMP_DIR

  def __init__(self, base_log_name):
    threading.Thread.__init__(self)
    self.log = open('/%s/%s.log' % (LG_TMP_DIR, base_log_name), 'a')
    self.exception = None

  def run(self):
    try:
      self.runme()
      print("ok", file=self.log)
    except Exception as e:
      self.exception = traceback.format_exc()
      print("\n%s\n" % self.exception, file=self.log)
    finally:
      self.finish()

class PopulateWorker(WorkerThread):
  def __init__(self, con, start_id, end_id, i, document_table):
    WorkerThread.__init__(self, 'populate-%d' % i)
    self.con = con
    con.autocommit(False)
    self.num = i
    self.start_id = start_id
    self.end_id = end_id
    self.document_table = document_table
    self.start_time = time.time()
    self.start()

  def finish(self):
    print("total time: %.2f s" % (time.time() - self.start_time), file=self.log)
    self.log.close()
    self.con.commit()
    self.con.close()

  def runme(self):
    print("populate thread-%d started" % self.num, file=self.log)
    cur = self.con.cursor()
    stmt = None
    for i in range(self.start_id, self.end_id):
      msg = get_msg(do_blob, i)
      stmt = get_insert(msg, i+1, self.document_table)
      cur.execute(stmt)
      if i % 100 == 0:
        self.con.commit()

def populate_table(con, num_records_before, do_blob, log, document_table):
  con.autocommit(False)
  cur = con.cursor()
  stmt = None
  workers = []
  N = int(num_records_before / 10)
  start_id = 0
  for i in range(10):
     w = PopulateWorker(MySQLdb.connect(user=user, host=host, port=port, db=db),
                        start_id, start_id + N, i, document_table)
     start_id += N
     workers.append(w)

  for i in range(start_id, num_records_before):
      msg = get_msg(do_blob, i)
      # print("length is %d, complen is %d" % (len(msg), len(zlib.compress(msg, 6))), file=self.log)
      stmt = get_insert(msg, i+1, document_table)
      cur.execute(stmt)

  con.commit()
  for w in workers:
    w.join()
    if w.exception:
      print("populater thead %d threw an exception" % w.num, file=log)
      return False
  return True

def get_update(msg, idx, document_table):
    if document_table:
        return """
               UPDATE t1 SET doc = '{"msg_prefix" : "%s", "msg" : "%s", "msg_length" : %d,
               "msg_checksum" : "%s"}' WHERE id=%d""" % (msg[0:255], msg, len(msg), sha1(msg), idx)
    else:
        return """
            UPDATE t1 SET msg_prefix='%s',msg='%s',msg_length=%d,
            msg_checksum='%s' WHERE id=%d """ % (msg[0:255], msg, len(msg), sha1(msg), idx)

def get_insert_on_dup(msg, idx, document_table):
    if document_table:
        return """
              INSERT INTO t1 (id, doc) VALUES
              (%d, '{"msg_prefix" : "%s", "msg": "%s", "msg_length" : %d,
                     "msg_checksum" : "%s"}')
              ON DUPLICATE KEY UPDATE
              id=VALUES(id),
              doc=VALUES(doc)
              """ % (idx, msg[0:255], msg, len(msg), sha1(msg))
    else:
        return """
            INSERT INTO t1 (msg_prefix,msg,msg_length,msg_checksum,id)
            VALUES ('%s','%s',%d,'%s',%d)
            ON DUPLICATE KEY UPDATE
            msg_prefix=VALUES(msg_prefix),
            msg=VALUES(msg),
            msg_length=VALUES(msg_length),
            msg_checksum=VALUES(msg_checksum),
            id=VALUES(id)""" % (msg[0:255], msg, len(msg), sha1(msg), idx)

def get_insert(msg, idx, document_table):
      if document_table:
          return """
              INSERT INTO t1 (id, doc) VALUES
              (%d, '{"msg_prefix" : "%s", "msg": "%s", "msg_length" : %d,
              "msg_checksum" : "%s"}')
              """ % (idx, msg[0:255], msg, len(msg), sha1(msg))
      else:
          return """
              INSERT INTO t1(id,msg_prefix,msg,msg_length,msg_checksum)
              VALUES (%d,'%s','%s',%d,'%s')
              """ % (idx, msg[0:255], msg, len(msg), sha1(msg))

def get_insert_null(msg, document_table):
    if document_table:
        return """
            INSERT INTO t1 (id, doc) VALUES
            (NULL, '{"msg_prefix" : "%s", "msg": "%s", "msg_length" : %d,
            "msg_checksum" : "%s"}')
            """ % (msg[0:255], msg, len(msg), sha1(msg))
    else:
        return """
            INSERT INTO t1 (msg_prefix,msg,msg_length,msg_checksum,id) VALUES
            ('%s','%s',%d,'%s',NULL)
            """ % (msg[0:255], msg, len(msg), sha1(msg))

class ChecksumWorker(WorkerThread):
  def __init__(self, con, checksum):
    WorkerThread.__init__(self, 'worker-checksum')
    self.con = con
    con.autocommit(False)
    self.checksum = checksum
    print("given checksum=%d" % checksum, file=self.log)
    self.start()

  def finish(self):
    print("total time: %.2f s" % (time.time() - self.start_time), file=self.log)
    self.log.close()
    self.con.close()

  def runme(self):
    print("checksum thread started", file=self.log)
    self.start_time = time.time()
    cur = self.con.cursor()
    cur.execute("SET SESSION innodb_lra_size=16")
    cur.execute("CHECKSUM TABLE t1")
    checksum = cur.fetchone()[1]
    self.con.commit()
    if checksum != self.checksum:
      errmsg = ("checksums do not match. given checksum=%d, "
                "calculated checksum=%d" % (self.checksum, checksum))
      print(errmsg, file=self.log)
      raise ChecksumError(errmsg)
    else:
      print("checksums match! (both are %d)" % checksum, file=self.log)

class Worker(WorkerThread):
  def __init__(self, num_xactions, xid, con, server_pid, do_blob, max_id,
               secondary_checks, document_table):
    WorkerThread.__init__(self, 'worker%02d' % xid)
    self.do_blob = do_blob
    self.xid = xid
    con.autocommit(False)
    self.con = con
    self.num_xactions = num_xactions
    cur = self.con.cursor()
    self.rand = random.Random()
    self.rand.seed(xid * server_pid)
    self.loop_num = 0
    self.max_id = max_id

    self.num_primary_select = 0
    self.num_secondary_select = 0
    self.num_secondary_only_select = 0
    self.num_inserts = 0
    self.num_deletes = 0
    self.num_updates = 0
    self.time_spent = 0
    self.secondary_checks = secondary_checks
    self.document_table = document_table
    self.start()

  def finish(self):
    print("loop_num:%d, total time: %.2f s" %\
      (self.loop_num, time.time() - self.start_time + self.time_spent), file=self.log)
    print("num_primary_select=%d,num_secondary_select=%d,num_secondary_only_select=%d" %\
      (self.num_primary_select, self.num_secondary_select, self.num_secondary_only_select), file=self.log)
    print("num_inserts=%d,num_updates=%d,num_deletes=%d,time_spent=%d" %\
      (self.num_inserts, self.num_updates, self.num_deletes, self.time_spent), file=self.log)
    self.log.close()

  def validate_msg(self, msg_prefix, msg, msg_length, msg_checksum, idx):

    prefix_match = msg_prefix == msg[0:255]

    checksum = sha1(msg)
    if type(msg_checksum) == bytes:
      msg_checksum = msg_checksum.decode('ascii')
    checksum_match = checksum == msg_checksum

    len_match = len(msg) == msg_length

    if not prefix_match or not checksum_match or not len_match:
      errmsg = "id(%d), length(%s,%d,%d), checksum(%s,%s,%s) prefix(%s,%s,%s)" % (
          idx,
          len_match, len(msg), msg_length,
          checksum_match, checksum, msg_checksum,
          prefix_match, msg_prefix, msg[0:255])
      print(errmsg, file=self.log)
      raise ValidateError(errmsg)
    else:
      print("Validated for length(%d) and id(%d)" % (msg_length, idx), file=self.log)

  # Check to see if the idx is in the first column of res_array
  def check_exists(self, res_array, idx):
    for res in res_array:
      if res[0] == idx:
        return True
    return False

  def runme(self):
    self.start_time = time.time()
    cur = self.con.cursor()
    print("thread %d started, run from %d to %d" % (
        self.xid, self.loop_num, self.num_xactions), file=self.log)

    while self.loop_num < self.num_xactions:
      idx = self.rand.randint(0, self.max_id)
      insert_or_update = self.rand.randint(0, 3)
      self.loop_num += 1

      try:
        stmt = None

        # Randomly toggle innodb_prefix_index_cluster_optimization 5%
        # of the time
        if self.rand.randint(0, 20) == 0:
          cur.execute("SET GLOBAL innodb_prefix_index_cluster_optimization="
                      "1-@@innodb_prefix_index_cluster_optimization")

        # Randomly change the value of innodb_zlib_wrap 2.77% of the time
        if self.rand.randint(0, 36) == 0:
          cur.execute("SET GLOBAL innodb_zlib_wrap=1-@@innodb_zlib_wrap");

        msg = get_msg(self.do_blob, idx)

        # Query primary key 70%, secondary key lookup 20%, secondary key only 10%
        r = self.rand.randint(1, 10)
        if r <= 7:
          if self.document_table:
            cur.execute("SELECT doc.msg_prefix,doc.msg,doc.msg_length, "
                        "doc.msg_checksum FROM t1 WHERE id=%d" % idx)
          else:
            cur.execute("SELECT msg_prefix,msg,msg_length,msg_checksum FROM t1 WHERE id=%d" % idx)
          res = cur.fetchone()
          self.num_primary_select += 1
        elif r <= 9:
          if self.document_table:
            cur.execute("SELECT doc.msg_prefix,doc.msg,doc.msg_length, "
                        "doc.msg_checksum FROM t1 use document keys WHERE doc.msg_prefix='%s'"
                        % msg[0:255])
          else:
            cur.execute("SELECT msg_prefix,msg,msg_length,msg_checksum FROM t1 WHERE msg_prefix='%s'" % msg[0:255])
          res = cur.fetchone()
          self.num_secondary_select += 1
        # Query only the secondary index
        else:
          if self.document_table:
            cur.execute("SELECT id, doc.msg_prefix FROM t1 use document keys "
                        "WHERE doc.msg_prefix='%s'" % msg[0:255])
          else:
            cur.execute("SELECT id, msg_prefix FROM t1 WHERE "
                        "msg_prefix='%s'" % msg[0:255])
          res = cur.fetchall()
          self.num_secondary_only_select += 1
        # Don't validate if r > 9 because we don't have sufficient columns.
        if r <= 9 and res:
          self.validate_msg(res[0], res[1], int(res[2]), res[3], idx)

        insert_with_index = False
        if insert_or_update:
          if res:
            if self.rand.randint(0, 1):
              stmt = get_update(msg, idx, self.document_table)
            else:
              stmt = get_insert_on_dup(msg, idx, self.document_table)
              insert_with_index = True
            self.num_updates += 1
          else:
            r = self.rand.randint(0, 2)
            if r == 0:
              stmt = get_insert(msg, idx, self.document_table)
              insert_with_index = True
            elif r == 1:
              stmt = get_insert_on_dup(msg, idx, self.document_table)
              insert_with_index = True
            else:
              stmt = get_insert_null(msg, self.document_table)
            self.num_inserts += 1
        else:
          stmt = "DELETE FROM t1 WHERE id=%d" % idx
          self.num_deletes += 1

        query_result = cur.execute(stmt)

        # 10% probability of checking to see the key exists in secondary index
        if self.secondary_checks and self.rand.randint(1, 10) == 1:
          if self.document_table:
            cur.execute("SELECT id, doc.msg_prefix FROM t1 use document keys WHERE "
                        "doc.msg_prefix='%s'" % msg[0:255])
          else:
            cur.execute("SELECT id, msg_prefix FROM t1 WHERE msg_prefix='%s'" % msg[0:255])
          res_array = cur.fetchall()
          if insert_or_update:
            if insert_with_index:
              if not self.check_exists(res_array, idx):
                print("Error: Inserted row doesn't exist in secondary index", file=self.log)
                raise Exception("Error: Inserted row doesn't exist in secondary index")
          else:
            if self.check_exists(res_array, idx):
              print("Error: Deleted row still exists in secondary index", file=self.log)
              raise Exception("Error: Deleted row still exists in secondary index")


        if (self.loop_num % 100) == 0:
          print("Thread %d loop_num %d: result %d: %s" % (self.xid,
                                                            self.loop_num, query_result,
                                                            stmt), file=self.log)

        # 30% commit, 10% rollback, 60% don't end the trx
        r = self.rand.randint(1,10)
        if r < 4:
          self.con.commit()
        elif r == 4:
          self.con.rollback()

      except (MySQLdb.OperationalError, MySQLdb.InternalError, MySQLdb.IntegrityError) as e:
        if is_connection_error(e):
          print("mysqld down, transaction %d" % self.xid, file=self.log)
          return
        else:
          print("mysql error for stmt(%s) %s" % (stmt, e), file=self.log)

    try:
      self.con.commit()
    except Exception as e:
      print("commit error %s" % e, file=self.log)


class DefragmentWorker(WorkerThread):
  def __init__(self, con):
    WorkerThread.__init__(self, 'worker-defragment')
    self.num_defragment = 0
    self.con = con
    self.con.autocommit(True)
    self.start_time = time.time()
    self.daemon = True
    self.stopped = False
    self.start()

  def reconnect(self, timeout=900):
    self.con = None
    SECONDS_BETWEEN_RETRY = 10
    attempts = 1
    print("Attempting to connect to MySQL Server", file=self.log)
    while not self.con and timeout > 0 and not self.stopped:
      try:
        self.con = MySQLdb.connect(user=user, host=host, port=port, db=db)
        if self.con:
          self.con.autocommit(False)
          self.cur = self.con.cursor()
          print("Connection successful after attempt %d" % attempts, file=self.log)
          break
      except (MySQLdb.OperationalError, MySQLdb.InternalError) as e:
        if not is_connection_error(e):
          raise e

      time.sleep(SECONDS_BETWEEN_RETRY)
      timeout -= SECONDS_BETWEEN_RETRY
      attempts += 1
    return self.con is None

  def stop(self):
    self.stopped = True

  def finish(self):
    print("defragment ran %d times" % self.num_defragment, file=self.log)
    print("total time: %.2f s" % (time.time() - self.start_time), file=self.log)
    self.log.close()
    if self.con:
        self.con.close()

  def runme(self):
    print("defragmentation thread started", file=self.log)
    cur = self.con.cursor()
    while not self.stopped:
      try:
        print("Starting defrag.", file=self.log)
        cur.execute("ALTER TABLE t1 DEFRAGMENT")
        print("Defrag completed successfully.", file=self.log)
        self.num_defragment += 1
        time.sleep(random.randint(0, 10))
      except (MySQLdb.OperationalError, MySQLdb.InternalError) as e:
        # Handle crash tests that kill the server while defragment runs.
        if is_connection_error(e):
          print("Server crashed while defrag was running.", file=self.log)
          self.reconnect()
        else:
          raise e


if  __name__ == '__main__':
  global LG_TMP_DIR

  pid_file = sys.argv[1]
  kill_db_after = int(sys.argv[2])
  num_records_before = int(sys.argv[3])
  num_workers = int(sys.argv[4])
  num_xactions_per_worker = int(sys.argv[5])
  user = sys.argv[6]
  host = sys.argv[7]
  port = int(sys.argv[8])
  db = sys.argv[9]
  do_blob = int(sys.argv[10])
  max_id = int(sys.argv[11])
  LG_TMP_DIR = sys.argv[12]
  checksum = int(sys.argv[13])
  secondary_checks = int(sys.argv[14])
  no_defrag = int(sys.argv[15])
  document_table = int(sys.argv[16])

  checksum_worker = None
  defrag_worker = None
  workers = []
  server_pid = int(open(pid_file).read())
  log = open('/%s/main.log' % LG_TMP_DIR, 'a')

#  print( "kill_db_after = ",kill_db_after," num_records_before = ", \)
#num_records_before, " num_workers= ",num_workers, "num_xactions_per_worker =",\
#num_xactions_per_worker, "user = ",user, "host =", host,"port = ",port,\
#" db = ", db, " server_pid = ", server_pid

  if num_records_before:
    print("populate table do_blob is %d" % do_blob, file=log)
    con = None
    retry = 3
    while not con and retry > 0:
        con = MySQLdb.connect(user=user, host=host, port=port, db=db)
        retry = retry - 1
    if not con:
        print("Cannot connect to MySQL after 3 attempts.", file=log)
        sys.exit(1)
    if not populate_table(con, num_records_before, do_blob, log,
                          document_table):
      sys.exit(1)
    con.close()

  if checksum:
    print("start the checksum thread", file=log)
    con = MySQLdb.connect(user=user, host=host, port=port, db=db)
    if not con:
        print("Cannot connect to MySQL server", file=log)
        sys.exit(1)

    checksum_worker = ChecksumWorker(con, checksum)
    workers.append(checksum_worker)

  print("start %d threads" % num_workers, file=log)
  for i in range(num_workers):
    worker = Worker(num_xactions_per_worker, i,
                    MySQLdb.connect(user=user, host=host, port=port, db=db),
                    server_pid, do_blob, max_id, secondary_checks,
                    document_table)
    workers.append(worker)

  if no_defrag == 0:
    defrag_worker = DefragmentWorker(MySQLdb.connect(user=user, host=host,
                                                     port=port, db=db))

  if kill_db_after:
    print("kill mysqld", file=log)
    time.sleep(kill_db_after)
    os.kill(server_pid, signal.SIGKILL)

  worker_failed = False
  print("wait for threads", file=log)
  for w in workers:
    w.join()
    if w.exception:
      print("Worker hit an exception:\n%s\n" % w.exception)
      worker_failed = True
  if defrag_worker:
    defrag_worker.stop()
    defrag_worker.join()
    if defrag_worker.exception:
      print ("Defrag worker hit an exception:\n%s\n." %
             defrag_worker.exception)
      worker_failed = True

  if checksum_worker:
    checksum_worker.join()
    if checksum_worker.exception:
      print ("Checksum worker hit an exception:\n%s\n." %
             checksum_worker.exception)
      worker_failed = True

  if worker_failed:
    sys.exit(1)

  print("all threads done", file=log)

