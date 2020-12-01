import time
import sys
import MySQLdb
from MySQLdb.constants import *
import argparse
import random
import threading
import traceback

NUM_WORKERS = 18
NUM_TRANSACTIONS = 100

def parse_args():
    parser = argparse.ArgumentParser(
        'tmp_disk_usage',
        description='Generate Temp Disk Usage Load',
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

def is_expected_error(exc):
    error_code = exc.args[0]
    return error_code == 50089

def generate_load(args, worker_id):

    op = worker_id % 3
    for i in range(NUM_TRANSACTIONS):
        con = MySQLdb.connect(user=args.user,
                            passwd=args.password,
                            host=args.host,
                            port=args.port,
                            db=args.database)

        try:
            print("WORKER %d: Executing iteration %d" % (worker_id, i))
            cursor = con.cursor()

            if op == 0 :
                cursor.execute('create temporary table tm(i int, c char(255));')
                cursor.execute('insert into tm select * from t1;')
                cursor.execute('drop temporary table tm;')

            if op == 1 :
                cursor.execute('select i, c, count(*) from t1 group by i, c having count(*) > 1;')

            if op == 2 :
                cursor.execute('select i, c from t1 order by hex(c) limit 1 offset 4000;')

            cursor.close()
        except MySQLdb.OperationalError as e:
            if not is_expected_error(e):
                raise e
        # Old versions of python-mysqldb don't support "err > CR_MAX_ERROR"
        except MySQLdb.InterfaceError as err:
            if "error totally whack" not in str(err):
                raise err

        con.close()

class worker_thread(threading.Thread):
    def __init__(self, args, worker_id, admin, readonly):
        threading.Thread.__init__(self)
        self.args = args
        self.exception = None
        self.admin = admin
        self.readonly = readonly
        self.worker_id = worker_id
        self.start()

    def run(self):
        try:
            if self.admin:
                run_admin_checks(self.args)
            elif self.readonly:
                run_reads(self.args)
            else:
                generate_load(self.args, self.worker_id)
        except Exception as e:
            self.exception = traceback.format_exc()

def main():
    args = parse_args()
    workers = []
    for i in range(NUM_WORKERS):
        worker = worker_thread(args, i, False, False)
        workers.append(worker)

    # admin_worker = worker_thread(args, NUM_WORKERS, True, False)
    # workers.append(admin_worker)

    # readonly_worker = worker_thread(args, NUM_WORKERS, False, True)
    # workers.append(readonly_worker)

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
