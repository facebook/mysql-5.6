import cStringIO
import MySQLdb
from MySQLdb.constants import CR
from MySQLdb.constants import ER
import os
import random
import signal
import sys
import threading
import time
import string
import traceback
import logging
import argparse
import array as arr
from sets import Set

# This is a stress rest for the drop cf feature. This test sends the following
# commands to mysqld server in parallel: create table, drop table, add index,
# drop index, manual compaction and drop cf, and verifies there are
# no race conditions or deadlocks.

# This rest uses 2 "metadata" table to record tables and column families
# that are used.
# The first table is called "tables".

# CREATE TABLE tables (
#    table_id int(5) NOT NULL,
#    created BOOLEAN NOT NULL,
#    primary_cf_id int(5),
#    secondary_cf_id int(5),
#    PRIMARY KEY (table_id),
#    KEY secondary_key (created, secondary_cf_id)
#    ) ENGINE=MEMORY;
#
# This table is populated before this script is run. Each row in this table
# represents a table on which create table, drop table, add index, drop index
# commands will run.
#
# The "created" column means if the table identified by "table_id"
# is created or not. If the table is not created, create table command
# can run on it. If the table is created, drop table command can run on it.
#
# "primary_cf_id" represents the column family the primary key is in. Thei
# primary key is created by create table command.
#
# "seconary_cf_id" represents the column family that secondary key is in.
# If it is NULL and the table is created, add index can run on the
# table. If it is not NULL and the table is created, drop index can
# run on the table.
#
# The second table is called "cfs".
# CREATE TABLE cfs (
#    cf_id int(5) NOT NULL,
#    used BOOLEAN NOT NULL,
#    ref_count int(5) NOT NULL,
#    PRIMARY KEY (cf_id)
#    ) ENGINE=MEMORY;

# This table is also populated before the script is run. Each row in this
# table represents a column family that can be used by create table and
# and add index commands.
#
# The "used" column will be true if the column family identified by
# "cf_id" has been created, but has not been dropped. It will be false
# if it has never been created or has been dropped.
#
# The "ref_count" column means the number of indexes that is stored
# in the column family.
#
# When drop cf and manual compaction command run, they will choose cfs
# with "used" being true to make the test more effective. In addition,
# drop cf will prefer cfs with ref_count being 0, i.e., the cfs don't
# contain any indexes.
#
# tables and cfs tables use MEMORY storage engine, which doesn't
# support transactions. As a result, the updates to these tables don't
# happen atomically in create table, drop table, add index, drop index
# operations.
#
# In this test we don't require that. ref_count is only used in drop cf
# operation to skew the selection towards the cfs that don't store any
# index. Temporary inconsistencies between tables and cfs tables don't cause
# correctness issues.
#
# Further, the locking granularity is table for MEMORY storge
# engine. When updating tables or cfs table, the table is locked.
# This doesn't cause perf issues for this test.

# global variable checked by threads to determine if the test is stopping
TEST_STOP = False
WEIGHTS = arr.array('i', [])
TOTAL_WEIGHT = 0

LOCKED_TABLE_IDS = set()
LOCK = threading.Lock()

# given a percentage value, rolls a 100-sided die and return whether the
# given value is above or equal to the die roll
#
# passing 0 should always return false and 100 should always return true
def roll_d100(p):
  assert p >= 0 and p <= 100
  return p >= random.randint(1, 100)

def execute(cur, stmt):
  ROW_COUNT_ERROR = 18446744073709551615L
  logging.debug("Executing %s" % stmt)
  cur.execute(stmt)
  if cur.rowcount < 0 or cur.rowcount == ROW_COUNT_ERROR:
    raise MySQLdb.OperationalError(MySQLdb.constants.CR.CONNECTION_ERROR,
                                   "Possible connection error, rowcount is %d"
                                   % cur.rowcount)

def is_table_exists_error(exc):
  error_code = exc.args[0]
  return (error_code == MySQLdb.constants.ER.TABLE_EXISTS_ERROR)

def is_table_not_found_error(exc):
  error_code = exc.args[0]
  return (error_code == MySQLdb.constants.ER.BAD_TABLE_ERROR)

def is_no_such_table_error(exc):
    error_code = exc.args[0]
    return (error_code == MySQLdb.constants.ER.NO_SUCH_TABLE)

def is_dup_key_error(exc):
    error_code = exc.args[0]
    return (error_code == MySQLdb.constants.ER.DUP_KEYNAME)

def is_cant_drop_key_error(exc):
    error_code = exc.args[0]
    return (error_code == MySQLdb.constants.ER.CANT_DROP_FIELD_OR_KEY)

def wait_for_workers(workers):
  logging.info("Waiting for %d workers", len(workers))

  # polling here allows this thread to be responsive to keyboard interrupt
  # exceptions, otherwise a user hitting ctrl-c would see the load_generator as
  # hanging and unresponsive
  try:
    while threading.active_count() > 1:
      time.sleep(1)
  except KeyboardInterrupt, e:
    os._exit(1)

  num_failures = 0
  for w in workers:
    w.join()
    if w.exception:
      logging.error(w.exception)
      num_failures += 1

  return num_failures

class WorkerThread(threading.Thread):
    def __init__(self, thread_id):
        threading.Thread.__init__(self)
        self.con = None
        self.cur = None
        self.num_requests = OPTIONS.num_requests
        self.loop_num = 0
        self.exception = None

        self.start_time = time.time()
        self.total_time = 0

        self.start()

    def insert_table_id(self, table_id):
        global LOCKED_TABLE_IDS
        global LOCK
        ret = False
        LOCK.acquire()

        if table_id not in LOCKED_TABLE_IDS:
            logging.debug("%d is added to locked table id set" % table_id)
            LOCKED_TABLE_IDS.add(table_id)
            ret = True

        LOCK.release()
        logging.debug("insert_table_id returns %s" % ret)
        return ret

    def remove_table_id(self, table_id):
        global LOCKED_TABLE_IDS
        global LOCK

        LOCK.acquire()
        logging.debug("%d is removed from locked table id set" % table_id)
        LOCKED_TABLE_IDS.remove(table_id)
        LOCK.release()

    def rollback_and_sleep(self):
        self.con.rollback()
        time.sleep(0.05)

    def insert_data_to_table(self, table_name):
        stmt = ("INSERT INTO %s VALUES (1, 1)"
            % table_name)
        for value in range(2, 4):
            stmt += ", (%d, %d)" % (value, value)
        execute(self.cur, stmt)

        self.con.commit()
        stmt = ("SET GLOBAL rocksdb_force_flush_memtable_now = true")
        execute(self.cur, stmt)

        stmt = ("INSERT INTO %s VALUES (4, 4)"
            % table_name)
        for value in range(5, 7):
            stmt += ", (%d, %d)" % (value, value)
        execute(self.cur, stmt)

        self.con.commit()
        stmt = ("SET GLOBAL rocksdb_force_flush_memtable_now = true")
        execute(self.cur, stmt)

    def handle_create_table(self):
        # create table
        # randomly choose a table that has not been created,
        # randomly choose a cf, and create the table
        logging.debug("handle create table")
        stmt = ("SELECT table_id FROM tables "
            "WHERE created = 0 ORDER BY RAND() LIMIT 1")
        execute(self.cur, stmt)

        if self.cur.rowcount != 1:
            logging.info("create table doesn't find a suitable table")
            self.rollback_and_sleep()
            return

        row = self.cur.fetchone()
        table_id = row[0]
        table_name = 'tbl%02d' % table_id

        stmt = ("SELECT cf_id FROM cfs "
            "ORDER BY RAND() LIMIT 1")
        execute(self.cur, stmt)

        row = self.cur.fetchone()
        primary_cf_id = row[0]
        primary_cf_name = 'cf-%02d' % primary_cf_id

        try:
            stmt = ("CREATE TABLE %s ("
                    "id1 int(5) unsigned NOT NULL,"
                    "id2 int(5) unsigned NOT NULL,"
                    "PRIMARY KEY (id1) COMMENT '%s'"
                    ") ENGINE=ROCKSDB" % (table_name, primary_cf_name))
            execute(self.cur, stmt)
        except MySQLdb.InterfaceError, e:
            self.rollback_and_sleep()
            return
        except Exception, e:
            if is_table_exists_error(e):
                self.rollback_and_sleep()
                return
            else:
                raise e

        self.insert_data_to_table(table_name)

        stmt = ("UPDATE tables "
            "SET created = 1, primary_cf_id = %d, secondary_cf_id = NULL "
            "WHERE table_id = %d" % (primary_cf_id, table_id))
        execute(self.cur, stmt)

        stmt = ("UPDATE cfs "
            "SET used = 1, ref_count = ref_count + 1 "
            "WHERE cf_id = %d" % primary_cf_id)
        execute(self.cur, stmt)

        self.con.commit()
        logging.debug("create table done")

    def handle_drop_table(self):
        # drop table
        # randomly choose a used table, and drop the table
        logging.info("handle drop table")

        stmt = ("SELECT table_id, primary_cf_id, secondary_cf_id FROM tables "
            "WHERE created = 1 ORDER BY RAND() LIMIT 1")
        execute(self.cur, stmt)

        if self.cur.rowcount != 1:
            logging.info("drop table doesn't find a suitable table")
            self.rollback_and_sleep()
            return

        row = self.cur.fetchone()
        table_id = row[0]

        if (not self.insert_table_id(table_id)):
            logging.info("can't drop table because the table is locked")
            self.rollback_and_sleep()
            return

        table_name = 'tbl%02d' % table_id
        primary_cf_id = row[1]
        secondary_cf_id = row[2]

        try:
            stmt = ("DROP TABLE %s" % table_name)
            execute(self.cur, stmt)
        except Exception, e:
            if is_table_not_found_error(e) :
                self.rollback_and_sleep()
                return
            else:
                raise e

        stmt = ("UPDATE tables "
            "SET created = 0, primary_cf_id = NULL, secondary_cf_id = NULL "
            "WHERE table_id = %d" % table_id)
        execute(self.cur, stmt)

        if primary_cf_id is not None:
            stmt = ("UPDATE cfs "
                "SET ref_count = ref_count - 1 "
                "WHERE cf_id = %d" % primary_cf_id)
            execute(self.cur, stmt)

        if secondary_cf_id is not None:
            stmt = ("UPDATE cfs "
                "SET ref_count = ref_count - 1 "
                "WHERE cf_id = %d" % secondary_cf_id)
            execute(self.cur, stmt)

        self.con.commit()
        self.remove_table_id(table_id)

        logging.debug("drop table done")

    def handle_add_index(self):
        # alter table add index
        # randomly choose a created table without secondary index,
        # add the secondary index
        logging.info("handle add index")

        stmt = ("SELECT table_id FROM tables "
            "WHERE created = 1 and secondary_cf_id is NULL "
            "ORDER BY RAND() LIMIT 1")
        execute(self.cur, stmt)

        if self.cur.rowcount != 1:
            logging.info("add index doesn't find a suitable table")
            self.rollback_and_sleep()
            return

        row = self.cur.fetchone()
        table_id = row[0]

        if (not self.insert_table_id(table_id)):
            logging.info("can't add index because the table is locked")
            self.rollback_and_sleep()
            return

        table_name = 'tbl%02d' % table_id

        stmt = ("SELECT cf_id FROM cfs "
            "ORDER BY RAND() LIMIT 1")
        execute(self.cur, stmt)

        row = self.cur.fetchone()
        secondary_cf_id = row[0]
        secondary_cf_name = 'cf-%02d' % secondary_cf_id

        try:
            stmt = ("ALTER TABLE %s "
                    "ADD INDEX secondary_key (id2) "
                    "COMMENT '%s'" % (table_name, secondary_cf_name))
            execute(self.cur, stmt)
        except MySQLdb.InterfaceError, e:
            self.rollback_and_sleep()
            return
        except Exception, e:
            if is_no_such_table_error(e) or is_dup_key_error(e) :
                self.rollback_and_sleep()
                return
            else:
                raise e

        stmt = ("UPDATE tables "
            "SET secondary_cf_id = %d "
            "WHERE table_id = %d" % (secondary_cf_id, table_id))
        execute(self.cur, stmt)

        stmt = ("UPDATE cfs "
            "SET used = 1, ref_count = ref_count + 1 "
            "WHERE cf_id = %d" % secondary_cf_id)
        execute(self.cur, stmt)

        self.con.commit()
        self.remove_table_id(table_id)
        logging.debug("add index done")

    def handle_drop_index(self):
        # alter table drop index
        # randomly choose a created table with secondary index
        # , and drop the index
        logging.info("handle drop index")

        stmt = ("SELECT table_id, secondary_cf_id FROM tables "
            "WHERE created = 1 and secondary_cf_id is NOT NULL "
            "ORDER BY RAND() LIMIT 1")
        execute(self.cur, stmt)

        if self.cur.rowcount != 1:
            logging.info("drop index doesn't find a suitable table")
            self.rollback_and_sleep()
            return

        row = self.cur.fetchone()
        table_id = row[0]

        if (not self.insert_table_id(table_id)):
            logging.info("can't drop index because the table is locked")
            self.rollback_and_sleep()
            return

        table_name = 'tbl%02d' % table_id
        secondary_cf_id = row[1]

        try:
            stmt = ("ALTER TABLE %s "
                    "DROP INDEX secondary_key" % table_name)
            execute(self.cur, stmt)
        except Exception, e:
            if is_no_such_table_error(e) or is_cant_drop_key_error(e) :
                self.rollback_and_sleep()
                return
            else:
                raise e

        stmt = ("UPDATE tables "
            "SET secondary_cf_id = NULL "
            "WHERE table_id = %d" % table_id)
        execute(self.cur, stmt)

        stmt = ("UPDATE cfs "
            "SET ref_count = ref_count - 1 "
            "WHERE cf_id = %d" % secondary_cf_id)
        execute(self.cur, stmt)

        self.con.commit()
        self.remove_table_id(table_id)
        logging.debug("drop index done")

    def handle_manual_compaction(self):
        # manual compaction
        # randomly choose a used cf
        # and do manual compaction on the cf
        logging.info("handle manual compaction")

        stmt = ("SELECT cf_id FROM cfs "
             " WHERE used = 1 "
             "ORDER BY RAND() LIMIT 1")
        execute(self.cur, stmt)

        if self.cur.rowcount != 1:
            logging.info("manual compaction doesn't find a suitable cf")
            self.rollback_and_sleep()
            return

        row = self.cur.fetchone()
        cf_id = row[0]
        cf_name = 'cf-%02d' % cf_id

        try:
            stmt = ("SET @@global.rocksdb_compact_cf = '%s'" % cf_name)
            execute(self.cur, stmt)
        except MySQLdb.InterfaceError, e:
            self.rollback_and_sleep()

        self.con.commit()
        logging.debug("manual compaction done")

    def handle_drop_cf(self):
        # drop cf
        # randomly choose a used cf
        # and try drop the cf
        logging.info("handle drop cf")

        stmt = None
        if roll_d100(90):
            stmt = ("SELECT cf_id FROM cfs "
                " WHERE used = 1 and ref_count = 0"
                " ORDER BY RAND() LIMIT 1")
        else:
            stmt = ("SELECT cf_id FROM cfs "
                " WHERE used = 1 and ref_count > 0"
                " ORDER BY RAND() LIMIT 1")
        execute(self.cur, stmt)

        if self.cur.rowcount != 1:
            logging.info("drop cf doesn't find a suitable cf")
            self.rollback_and_sleep()
            return

        row = self.cur.fetchone()
        cf_id = row[0]
        cf_name = 'cf-%02d' % cf_id

        try:
            stmt = ("SET @@global.rocksdb_delete_cf = '%s'" % cf_name)
            execute(self.cur, stmt)
        except MySQLdb.InterfaceError, e:
            self.rollback_and_sleep()
            return

        stmt = ("UPDATE cfs "
            "SET used = 0 "
            "WHERE cf_id = %d" % cf_id)
        execute(self.cur, stmt)

        self.con.commit()
        logging.debug("drop cf done")

    def runme(self):
        global TEST_STOP
        global WEIGHTS
        global TOTAL_WEIGHT

        self.con = MySQLdb.connect(user=OPTIONS.user, host=OPTIONS.host,
                           port=OPTIONS.port, db=OPTIONS.db)

        if not self.con:
            raise Exception("Unable to connect to mysqld server")

        self.con.autocommit(False)
        self.cur = self.con.cursor()

        while self.loop_num < self.num_requests and not TEST_STOP:
            p = random.randint(1, TOTAL_WEIGHT)
            if p <= WEIGHTS[0]:
                # create table
                self.handle_create_table()
            elif p <= WEIGHTS[1]:
                # drop table
                self.handle_drop_table()
            elif p <= WEIGHTS[2]:
                # alter table add index
                self.handle_add_index()
            elif p <= WEIGHTS[3]:
                # alter table drop index
                self.handle_drop_index()
            elif p <= WEIGHTS[4]:
                # drop cf
                self.handle_drop_cf()
            elif p <= WEIGHTS[5]:
                # manual compaction
                self.handle_manual_compaction()
            self.loop_num += 1

    def run(self):
        global TEST_STOP

        try:
            logging.info("Started")
            self.runme()
            logging.info("Completed successfully")
        except Exception, e:
            self.exception = traceback.format_exc()
            logging.error(self.exception)
            TEST_STOP = True
        finally:
            self.total_time = time.time() - self.start_time
            logging.info("Total run time: %.2f s" % self.total_time)
            self.con.close()

if  __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Drop cf stress')

    parser.add_argument('-d, --db', dest='db', default='test',
                        help="mysqld server database to test with")

    parser.add_argument('-H, --host', dest='host', default='127.0.0.1',
                        help="mysqld server host ip address")

    parser.add_argument('-L, --log-file', dest='log_file', default=None,
                        help="log file for output")

    parser.add_argument('-w, --num-workers', dest='num_workers', type=int,
                        default=16,
                        help="number of worker threads to test with")

    parser.add_argument('-P, --port', dest='port', default=3307, type=int,
                        help='mysqld server host port')

    parser.add_argument('-r, --num-requests', dest='num_requests', type=int,
                        default=100000000,
                        help="number of requests issued per worker thread")

    parser.add_argument('-u, --user', dest='user', default='root',
                        help="user to log into the mysql server")

    parser.add_argument('-v, --verbose', dest='verbose', action='store_true',
                        help="enable debug logging")

    parser.add_argument('--ct-weight', dest='create_table_weight', type=int,
                        default=3,
                        help="weight of create table command")

    parser.add_argument('--dt-weight', dest='drop_table_weight', type=int,
                        default=3,
                        help="weight of drop table command")

    parser.add_argument('--ai-weight', dest='add_index_weight', type=int,
                        default=3,
                        help="weight of add index command")

    parser.add_argument('--di-weight', dest='drop_index_weight', type=int,
                        default=3,
                        help="weight of drop index command")

    parser.add_argument('--dc-weight', dest='drop_cf_weight', type=int,
                        default=3,
                        help="weight of drop cf command")

    parser.add_argument('--mc-weight', dest='manual_compaction_weight',
                        type=int,
                        default=1,
                        help="weight of manual compaction command")

    OPTIONS = parser.parse_args()

    if OPTIONS.verbose:
        log_level = logging.DEBUG
    else:
        log_level = logging.INFO

    logging.basicConfig(level=log_level,
                    format='%(asctime)s %(threadName)s [%(levelname)s] '
                           '%(message)s',
                    datefmt='%Y-%m-%d %H:%M:%S',
                    filename=OPTIONS.log_file)

    con = MySQLdb.connect(user=OPTIONS.user, host=OPTIONS.host,
                           port=OPTIONS.port, db=OPTIONS.db)

    if not con:
        raise Exception("Unable to connect to mysqld server")

    TOTAL_WEIGHT += OPTIONS.create_table_weight
    WEIGHTS.append(TOTAL_WEIGHT)

    TOTAL_WEIGHT += OPTIONS.drop_table_weight;
    WEIGHTS.append(TOTAL_WEIGHT)

    TOTAL_WEIGHT += OPTIONS.add_index_weight;
    WEIGHTS.append(TOTAL_WEIGHT)

    TOTAL_WEIGHT += OPTIONS.drop_index_weight;
    WEIGHTS.append(TOTAL_WEIGHT)

    TOTAL_WEIGHT += OPTIONS.drop_cf_weight;
    WEIGHTS.append(TOTAL_WEIGHT)

    TOTAL_WEIGHT += OPTIONS.manual_compaction_weight;
    WEIGHTS.append(TOTAL_WEIGHT)

    workers = []
    for i in xrange(OPTIONS.num_workers):
        workers.append(WorkerThread(i))

    workers_failed = 0
    workers_failed += wait_for_workers(workers)
    if workers_failed > 0:
        logging.error("Test detected %d failures, aborting" % workers_failed)
        sys.exit(1)

    cur = con.cursor()
    stmt = ("SELECT table_name FROM information_schema.tables "
        "WHERE table_name like 'tbl%'")
    execute(cur, stmt)

    for row in cur.fetchall():
        table_name = row[0]
        stmt = ("DROP TABLE %s" % table_name)
        execute(cur, stmt)

    stmt = ("SELECT table_name FROM information_schema.tables "
        "WHERE table_name like 'cf-%'")
    execute(cur, stmt)

    for row in cur.fetchall():
        table_name = row[0]
        stmt = ("DROP TABLE %s" % table_name)
        execute(cur, stmt)

    con.close()

    sys.exit(0)

