import cStringIO
import hashlib
import MySQLdb
import os
import random
import signal
import sys
import threading
import time
import string

CHARS = string.letters + string.digits

def sha1(x):
  return hashlib.sha1(str(x)).hexdigest()

def get_msg():
  blob_length = random.randint(1, 255)
  if random.randint(1, 2) == 1:
    # blob that cannot be compressed (well, compresses to 85% of original size)
    return ''.join([random.choice(CHARS) for x in xrange(blob_length)])
  else:
    # blob that can be compressed
    return random.choice(CHARS) * blob_length

def get_insert(idx):
  msg = get_msg()
  return """
REPLACE INTO t1 (id,msg,msg_length,msg_checksum) VALUES (%d,'%s',%d,'%s')""" % (
idx, msg, len(msg), sha1(msg))

class Inserter(threading.Thread):
  Instance = None
  def __init__(self, con, num_inserts, max_id):
    threading.Thread.__init__(self)
    self.finished = False
    self.max_id = max_id
    self.num_inserts = num_inserts
    con.autocommit(False)
    self.con = con
    self.rand = random.Random()
    Inserter.Instance = self
    self.start()
  def run(self):
    try:
      self.runme()
      print "Inserter OK."
    except Exception, e:
      print "caught (%s)" % e
    finally:
      self.finish()
  def runme(self):
    cur = self.con.cursor()
    for i in xrange(self.num_inserts):
      idx = self.rand.randint(0, self.max_id)
      try:
        cur.execute(get_insert(idx))
        # 30% commit, 70% don't end the trx
        r = self.rand.randint(1,10)
        if r < 4:
          self.con.commit()
      except:
        cur = self.con.cursor()
    try:
      self.con.commit()
    except:
      pass
  def finish(self):
    print "Inserter thread quitting."
    self.finished = True

class Deleter(threading.Thread):
  def __init__(self, con, max_id):
    threading.Thread.__init__(self)
    self.max_id = max_id
    con.autocommit(False)
    self.con = con
    self.rand = random.Random()
    self.start()
  def run(self):
    try:
      self.runme()
      print "Deleter OK."
    except Exception, e:
      print "caught (%s)" % e
    finally:
      self.finish()
  def runme(self):
    global inserter
    cur = self.con.cursor()
    while not Inserter.Instance.finished:
      idx = self.rand.randint(0, self.max_id)
      try:
        cur.execute("DELETE FROM t1 WHERE id=%d" % idx)
        # 30% commit, 70% don't end the trx
        r = self.rand.randint(1,10)
        if r < 4:
          self.con.commit()
      except:
        cur = self.con.cursor()
    try:
      self.con.commit()
    except:
      pass
  def finish(self):
    print "Deleter thread quitting."

if __name__ == '__main__':
  user = sys.argv[1]
  host = sys.argv[2]
  port = int(sys.argv[3])
  db = 'test'
  num_inserts = 20000
  max_id = 1000
  inserter = Inserter(
    MySQLdb.connect(user=user, host=host, port=port, db=db),
    num_inserts,
    max_id)
  deleter = Deleter(
    MySQLdb.connect(user=user, host=host, port=port, db=db),
    max_id)
  inserter.join()
  deleter.join()
