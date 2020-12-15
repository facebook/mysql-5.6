import time
import sys
import MySQLdb
from MySQLdb.constants import *
import argparse
import random
import threading
import traceback

NUM_WORKERS = 50
NUM_TRANSACTIONS = 1000

def parse_args():
    parser = argparse.ArgumentParser(
        'max_db_connections',
        description='Generate Connections Load',
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
        '--db_prefix',
        type=str,
        help='database prefix')
    parser.add_argument(
        '--db_count',
        type=int,
        help='upper limit on database suffix')

    return parser.parse_args()

def is_expected_error(exc):
    error_code = exc.args[0]
    # ER_MULTI_TENANCY_MAX_CONNECTION (50039)
    # ER_BAD_DB_ERROR (1049)
    return error_code == 50039 or error_code == 1049

def generate_load(args, worker_id):
    for i in range(NUM_TRANSACTIONS):
        try:
            db_name = "%s%d" % (args.db_prefix, random.randint(1, args.db_count))
            con = MySQLdb.connect(user=args.user,
                                passwd=args.password,
                                host=args.host,
                                port=args.port,
                                db=db_name)

            # print("WORKER %d: Executing iteration %d" % (worker_id, i))
            cursor = con.cursor()
            cursor.execute('select 1;')
            cursor.close()
            con.close()
        except MySQLdb.OperationalError as e:
            if not is_expected_error(e):
                raise e
        # Old versions of python-mysqldb don't support "err > CR_MAX_ERROR"
        except MySQLdb.InterfaceError as err:
            if "error totally whack" not in str(err):
                raise err

class worker_thread(threading.Thread):
    def __init__(self, args, worker_id):
        threading.Thread.__init__(self)
        self.args = args
        self.exception = None
        self.worker_id = worker_id
        self.start()

    def run(self):
        try:
            generate_load(self.args, self.worker_id)
        except Exception as e:
            self.exception = traceback.format_exc()

def main():
    args = parse_args()
    workers = []
    for i in range(NUM_WORKERS):
        worker = worker_thread(args, i)
        workers.append(worker)

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
