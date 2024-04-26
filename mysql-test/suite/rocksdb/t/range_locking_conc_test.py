"""
This script tests Range Locking. See
  mysql-test/suite/rocksdb/t/range_locking_conc_test.txt for details.

Usage:

  python3 suite/rocksdb/t/range_locking_conc_test.py \
          [--progress] [--verbose] \
          root 127.0.0.1 $MASTER_MYPORT test t1 \
          num_inserts   num_insert_threads \
          num_group_ops num_group_threads


  For Example:

    time python3 suite/rocksdb/t/range_locking_conc_test.py --progress root 127.0.0.1 3314 test t1 2000 20 10000 40

"""

import hashlib
import MySQLdb
from MySQLdb.constants import ER
import os
import random
import signal
import sys
import threading
import time
import string
import traceback

# MAX_PK_VAL = 1000*1000*1000
MAX_PK_VAL = 1000 * 1000

show_progress = False
verbose_output = False

counter_n_inserted = 0
counter_n_deleted = 0
counter_n_insert_failed = 0
counter_n_groups_created = 0
counter_n_group_create_fails = 0
counter_n_groups_verified = 0
counter_n_groups_deleted = 0


def is_lock_error(exc):
    error_code = exc.args[0]
    return error_code == ER.LOCK_WAIT_TIMEOUT or error_code == ER.LOCK_DEADLOCK


#
#  Watcher prints the test progress and some status variables once per second
#
class Watcher(threading.Thread):
    Instance = None

    def __init__(self, con):
        threading.Thread.__init__(self)
        self.should_stop = False
        self.finished = False
        self.con = con
        self.start()

    def run(self):
        global counter_n_inserted
        global counter_n_deleted
        global counter_n_insert_failed
        global counter_n_groups_created
        global counter_n_group_create_fails
        global counter_n_groups_verified
        global counter_n_groups_deleted
        event = threading.Event()

        save_counter_n_inserted = 0
        save_counter_n_deleted = 0
        save_counter_n_insert_failed = 0
        save_counter_n_groups_created = 0
        save_counter_n_group_create_fails = 0
        save_counter_n_groups_verified = 0
        save_counter_n_groups_deleted = 0
        save_wait_count = 0
        n = 0
        cur = self.con.cursor()
        try:
            while not self.should_stop:
                event.wait(1)

                cur.execute("show status like '%rocksdb_locktree_wait_count%'")
                row = cur.fetchone()
                wait_count = int(row[1])

                print("== %d ========================" % n)
                print(
                    "counter_n_inserted=%d"
                    % (counter_n_inserted - save_counter_n_inserted)
                )
                print(
                    "counter_n_deleted=%d"
                    % (counter_n_deleted - save_counter_n_deleted)
                )
                print(
                    "counter_n_insert_failed=%d"
                    % (counter_n_insert_failed - save_counter_n_insert_failed)
                )
                print(
                    "counter_n_groups_created=%d"
                    % (counter_n_groups_created - save_counter_n_groups_created)
                )
                print(
                    "counter_n_group_create_fails=%d"
                    % (counter_n_group_create_fails - save_counter_n_group_create_fails)
                )
                print(
                    "counter_n_groups_verified=%d"
                    % (counter_n_groups_verified - save_counter_n_groups_verified)
                )
                print(
                    "counter_n_groups_deleted=%d"
                    % (counter_n_groups_deleted - save_counter_n_groups_deleted)
                )
                print("wait_count=%d" % (wait_count - save_wait_count))

                save_counter_n_inserted = counter_n_inserted
                save_counter_n_deleted = counter_n_deleted
                save_counter_n_insert_failed = counter_n_insert_failed
                save_counter_n_groups_created = counter_n_groups_created
                save_counter_n_group_create_fails = counter_n_group_create_fails
                save_counter_n_groups_verified = counter_n_groups_verified
                save_counter_n_groups_deleted = counter_n_groups_deleted
                save_wait_count = wait_count
                n += 1

        except Exception as e:
            self.exception = traceback.format_exc()
            print("Watcher caught (%s)" % (e))

        finally:
            self.finish()

    def finish(self):
        n = 0
        # Do nothing


#
#  A worker is one client thread producing the benchmark workload
#
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
        my_id = threading.get_ident()
        try:
            self.cur = self.con.cursor()
            if self.worker_type == "insert":
                self.run_inserts()
            if self.worker_type == "join_group":
                self.run_create_groups()
        except Exception as e:
            print(e)
            self.exception = traceback.format_exc()
            print("THR %d caught (%s)" % (my_id, e))

        finally:
            self.finish()

    #
    #  Insert one row, making sure this doesn't break any groups
    #
    def run_one_insert(self):
        global counter_n_inserted
        global counter_n_deleted
        global counter_n_insert_failed
        cur = self.cur
        # pk = self.rand.randint(1, MAX_PK_VAL)
        # Note: here the distribution is intentionally 2x wider than the grouping
        # thread has.
        pk = int(self.rand.normalvariate(MAX_PK_VAL / 2, MAX_PK_VAL / 50.0))

        cur.execute("begin")
        do_commit = False
        cur.execute(
            "select pk,parent_id,group_list from t1 where pk>=%s limit 1 for update",
            (pk,),
        )
        row = cur.fetchone()
        group_to_delete = None
        if row is None:
            # print("No row found, inserting %d" % (pk+1000*1000))
            cur.execute("insert into t1 (pk) values(%s)", (pk + 1000 * 1000,))
            do_commit = True
        else:
            if (row[0] - pk) > 2 and row[1] is None:
                # print("Row found, inserting into gap, %d" % pk)
                cur.execute("insert into t1 (pk) values(%s)", (pk,))
                do_commit = True
            else:
                # print("Row found, grouped or too tight")
                if row[2]:
                    # if parent_id is present, use it
                    group_to_delete = row[0]
                    # print("About to delete %d" % group_to_delete)
                do_commit = False

        if do_commit:
            cur.execute("commit")
            counter_n_inserted += 1
            return 1
        else:
            counter_n_insert_failed += 1
            if group_to_delete:
                counter_n_deleted += 5
                self.delete_group(group_to_delete, True)
                cur.execute("commit")
            else:
                cur.execute("rollback")
            return 0

    def run_one_create_group(self):
        global counter_n_groups_created
        global counter_n_group_create_fails
        global counter_n_groups_deleted
        cur = self.cur
        # pk = self.rand.randint(1, MAX_PK_VAL)
        pk = int(self.rand.normalvariate(MAX_PK_VAL / 2, MAX_PK_VAL / 100))

        n_rows = 0
        n_groups_deleted = 0
        first_pk = None
        cur.execute(
            "select pk,parent_id,group_list from t1 where pk>=%s limit 5 for update",
            (pk,),
        )
        row = cur.fetchone()
        while row is not None:
            if first_pk is None:
                first_pk = row[0]
                group_list = str(first_pk)
            else:
                group_list = group_list + "," + str(row[0])

            last_pk = row[0]
            if row[1] is not None:
                # Found a row in a group
                # Continue until group end.
                found_next_group = False
                row = cur.fetchone()
                while row is not None:
                    if row[1] is None:
                        found_next_group = True
                        first_pk = row[0]
                        group_list = str(first_pk)
                        break
                    row = cur.fetchone()

                if not found_next_group:
                    break

            if row[2] is not None:
                # Found a group leader row.
                ungrouped_ids = self.delete_group(row[0], False)
                n_groups_deleted += 1
                i = 1
                n_rows += 1
                while n_rows < 5:
                    group_list = group_list + "," + str(ungrouped_ids[i])
                    last_pk = ungrouped_ids[i]
                    i += 1
                    n_rows += 1
                break
            n_rows += 1
            row = cur.fetchone()

        if n_rows == 5 or n_groups_deleted > 0:
            # Ok we got 5 rows in a row and they are all standalone
            # Create a group.
            # print("Creating group %d" % first_pk)
            cur.execute(
                "update t1 set group_list=%s where pk=%s",
                (
                    group_list,
                    first_pk,
                ),
            )
            cur.execute(
                "update t1 set parent_id=%s where pk > %s and pk <=%s",
                (first_pk, first_pk, last_pk),
            )
            cur.execute("commit")
            counter_n_groups_created += 1
            counter_n_groups_deleted += n_groups_deleted
            return 1
        else:
            # print("Failed to join a group")
            counter_n_group_create_fails += 1
            cur.execute("rollback")
            return 0

    #
    # Verify and delete the group
    #   @return  An array listing the deleted PKs
    #
    def delete_group(self, group_id, delete_rows):
        global counter_n_groups_verified
        cur = self.con.cursor()
        cur.execute(
            "select pk,parent_id,group_list from t1 where pk>=%s limit 5 for update",
            (group_id,),
        )
        first_pk = None
        n_rows = 0

        row = cur.fetchone()
        while row is not None:
            if first_pk is None:
                first_pk = row[0]
                group_list = str(first_pk)
                group_arr = []
                group_arr.append(first_pk)
                group_list_base = row[2]
                if first_pk != group_id:
                    self.raise_error("First row is not the group leader!")
            else:
                group_list = group_list + "," + str(row[0])
                group_arr.append(row[0])

            last_pk = row[0]
            if row[0] != first_pk and row[1] != first_pk:
                self.raise_error(
                    "Row in group has wrong parent_id (expect %s got %s)"
                    % (first_pk, row[1])
                )
                break
            if row[0] != first_pk and row[2] is not None:
                self.raise_error("Row in group is a group leader?")
                break
            n_rows += 1
            row = cur.fetchone()

        if n_rows != 5:
            self.raise_error(
                "Expected %d rows got %d"
                % (
                    5,
                    n_rows,
                )
            )
        if group_list != group_list_base:
            self.raise_error(
                "Group contents mismatch: expected '%s' got '%s'"
                % (group_list_base, group_list)
            )
        # Ok, everything seems to be in order.
        if delete_rows:
            cur.execute(
                "delete from t1 where pk>=%s and pk<=%s",
                (
                    group_id,
                    last_pk,
                ),
            )
        else:
            cur.execute(
                "update t1 set parent_id=NULL, group_list=NULL where pk>=%s and pk<=%s",
                (
                    group_id,
                    last_pk,
                ),
            )

        counter_n_groups_verified += 1
        return group_arr

    def raise_error(self, msg):
        print("Data corruption detected: " + msg)
        sys.exit("Failed!")

    def run_inserts(self):
        # print("Worker.run_inserts")
        i = 0
        while i < self.num_inserts:
            try:
                i += self.run_one_insert()
            except MySQLdb.OperationalError as e:
                self.con.rollback()
                cur = self.con.cursor()
                if not is_lock_error(e):
                    raise e

    def run_create_groups(self):
        # print("Worker.run_create_groups")
        i = 0
        while i < self.num_inserts:
            try:
                i += self.run_one_create_group()
            except MySQLdb.OperationalError as e:
                self.con.rollback()
                cur = self.con.cursor()
                if not is_lock_error(e):
                    raise e

    def finish(self):
        self.finished = True


if __name__ == "__main__":
    if len(sys.argv) != 10 and len(sys.argv) != 11:
        print(
            "Usage: range_locking_conc_test.py "
            "[--progress] "
            "user host port db_name table_name "
            "num_inserts num_insert_threads "
            "num_grp_ops num_group_threads"
        )
        sys.exit(1)
    i = 1
    if sys.argv[i] == "--progress":
        show_progress = True
        i += 1

    if sys.argv[i] == "--verbose":
        verbose_output = True
        i += 1

    user = sys.argv[i]
    i += 1

    host = sys.argv[i]
    i += 1

    port = int(sys.argv[i])
    i += 1

    db = sys.argv[i]
    i += 1

    table_name = sys.argv[i]
    i += 1

    num_inserts = int(sys.argv[i])
    i += 1

    num_insert_workers = int(sys.argv[i])
    i += 1

    num_group_ops = int(sys.argv[i])
    i += 1

    num_group_workers = int(sys.argv[i])
    i += 1

    con = MySQLdb.connect(user=user, host=host, port=port, db=db)
    con.cursor().execute("set global rocksdb_lock_wait_timeout=20")
    con.cursor().execute("drop table if exists t1")
    con.cursor().execute(
        "create table t1 ( "
        "  pk bigint primary key, "
        "  group_list varchar(128), "
        "  parent_id bigint "
        ") engine=rocksdb;"
    )

    worker_failed = False
    workers = []
    worker_type = "insert"
    num_loops = num_inserts
    for i in range(num_insert_workers + num_group_workers):
        worker = Worker(
            MySQLdb.connect(user=user, host=host, port=port, db=db),
            table_name,
            worker_type,
            num_loops,
        )
        workers.append(worker)
        if i == num_insert_workers - 1:
            worker_type = "join_group"
            num_loops = num_group_ops

    # A watcher thread to print the statistics periodically
    if show_progress:
        watcher = Watcher(MySQLdb.connect(user=user, host=host, port=port, db=db))

    for w in workers:
        w.join()
        if w.exception:
            print("Worker hit an exception:\n%s\n" % w.exception)
            worker_failed = True

    # Stop the watcher
    if show_progress:
        watcher.should_stop = True
        watcher.join()

    if verbose_output:
        print("\n")
        print("rows_inserted: %d" % counter_n_inserted)
        print("rows_deleted: %d" % counter_n_deleted)
        print("rows_insert_failed: %d" % counter_n_insert_failed)
        print("groups_created:  %d" % counter_n_groups_created)
        print("groups_verified: %d" % counter_n_groups_verified)
        print("groups_deleted:  %d" % counter_n_groups_deleted)
        print("group_create_fails: %d" % counter_n_group_create_fails)

    if worker_failed:
        sys.exit(1)
