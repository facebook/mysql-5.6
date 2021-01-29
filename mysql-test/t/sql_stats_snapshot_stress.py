import datetime
import time
import sys
import MySQLdb
from MySQLdb.constants import *
import argparse
import random
import threading
import traceback

NUM_WORKERS = 10

def parse_args():
    parser = argparse.ArgumentParser(
        'sql_stats_snapshot_stress',
        description='Generate load populating sql stats and using snapshot',
    )
    parser.add_argument(
        '--host',
        type=str,
        help='host name')
    parser.add_argument(
        '--port',
        type=int,
        help='port number')
    parser.add_argument(
        '--user',
        type=str,
        help='user name')
    parser.add_argument(
        '--password',
        default='',
        type=str,
        help='password')
    parser.add_argument(
        '--database',
        type=str,
        help='database to use')

    return parser.parse_args()

def generate_load(worker):
    args = worker.args
    worker_id = worker.worker_id
    con = MySQLdb.connect(user=args.user,
                          passwd=args.password,
                          host=args.host,
                          port=args.port,
                          db=args.database)
    i = 0
    if worker_id >= 2:
        op = 1
    else:
        op = 2

    while not worker.stop_flag:
        # op = random.randint(1, 10)
        start_time = datetime.datetime.now()
        cur = con.cursor()
        try:
            if op == 1:
                op_str = "insert"
                t = random.randint(1, 1000)
                table = "tt" + str(t)
                cur.execute("create temporary table %s (a int)" % table)
                stmt = "select * from " + table
                for n in range(0, 100):
                    stmt = stmt + " union select " + str(n)
                    cur.execute(stmt)
                cur.execute("drop table " + table)
            else:
                time.sleep(random.randint(1, 5))
                op_str = "snapshot"
                print("WORKER %d: Starting %s %d ..." % (worker_id, op_str, i))
                cur = con.cursor()
                cur.execute( \
                    "select s.execution_count, " \
                    " left(t.sql_text, 50) text " \
                    "from information_schema.sql_text t, " \
                    " information_schema.sql_statistics s, " \
                    " information_schema.client_attributes a " \
                    "where a.client_id=s.client_id and s.sql_id=t.sql_id " \
                    "order by 1 desc, 2")
        except (MySQLdb.OperationalError, MySQLdb.InternalError) as e:
            if not is_deadlock_error(e):
                raise e

        cur.close()
        delta = datetime.datetime.now() - start_time
        print("WORKER %d: Executed %s %d in %d ms" % (worker_id, op_str, i, \
            delta.total_seconds() * 1000))
        i += 1

    con.close()

class worker_thread(threading.Thread):
    def __init__(self, args, worker_id):
        threading.Thread.__init__(self)
        self.args = args
        self.exception = None
        self.worker_id = worker_id
        self.stop_flag = False
        self.start()

    def run(self):
        try:
            generate_load(self)
        except Exception as e:
            self.exception = traceback.format_exc()

    def stop(self):
        self.stop_flag = True

def main():
    args = parse_args()
    workers = []
    for i in range(NUM_WORKERS):
        worker = worker_thread(args, i)
        workers.append(worker)

    time.sleep(60)

    for w in workers:
        w.stop()

    worker_failed = False
    for w in workers:
        w.join()
        if w.exception:
            print("worker hit an exception:\n%s\n'" % w.exception)
            worker_failed = True

    if worker_failed:
        sys.exit(1)

if __name__ == '__main__':
  main()
