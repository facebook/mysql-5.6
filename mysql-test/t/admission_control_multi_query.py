import time
import sys
import MySQLdb
import argparse
import random
import threading
import traceback

NUM_WORKERS = 50
NUM_TRANSACTIONS = 2500

def parse_args():
    parser = argparse.ArgumentParser(
        'multi_query',
        description='Generate Multi query load',
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

def generate_load(args):
    con = MySQLdb.connect(user=args.user,
                          passwd=args.password,
                          host=args.host,
                          port=args.port,
                          db=args.database)
    for i in range(NUM_TRANSACTIONS):
        cursor = con.cursor()
        cursor.execute('begin;')
        sql = ''
        values = []
        for i in range(3):
            val = random.randrange(1, 50)
            values.append(val)
        values = sorted(values)
        for val in values:
            sql += 'insert into t1 values(%d, 1) on duplicate key ' \
                   'update b=greatest(b+1, 0);' % (val)
        sql += 'commit;'
        cursor.execute(sql)
        cursor.close()
    con.close()

def run_admin_checks(args):
    con = MySQLdb.connect(user=args.user,
                          passwd=args.password,
                          host=args.host,
                          port=args.port,
                          db=args.database)
    cursor=con.cursor()
    cursor.execute("select @@global.max_running_queries")
    rows = cursor.fetchone()
    max_running_queries = int(rows[0])
    for i in range(NUM_TRANSACTIONS):
        cursor=con.cursor()
        cursor.execute("show status like '%admission%'")
        rows = cursor.fetchall()
        if int(rows[1][1]) > max_running_queries:
            raise Exception('Current running queries %s is more than ' \
                            'max_running_queries %d' % (rows[1][1],
                             max_running_queries))

class worker_thread(threading.Thread):
    def __init__(self, args, admin):
        threading.Thread.__init__(self)
        self.args = args
        self.exception = None
        self.admin = admin
        self.start()

    def run(self):
        try:
            if self.admin:
                run_admin_checks(self.args)
            else:
                generate_load(self.args)
        except Exception as e:
            self.exception = traceback.format_exc()

def main():
    args = parse_args()
    workers = []
    for i in range(NUM_WORKERS):
        worker = worker_thread(args, False)
        workers.append(worker)

    admin_worker = worker_thread(args, True)
    workers.append(admin_worker)

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
