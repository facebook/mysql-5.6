import time
import sys
import MySQLdb
import argparse
import random
import threading
import traceback

NUM_WORKERS = 50
NUM_TRANSACTIONS = 10000

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
    for i in xrange(NUM_TRANSACTIONS):
        cursor = con.cursor()
        cursor.execute('begin;')
        sql = ''
        values = []
        for i in xrange(3):
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

class worker_thread(threading.Thread):
    def __init__(self, args):
        threading.Thread.__init__(self)
        self.args = args
        self.exception = None
        self.start()

    def run(self):
        try:
            generate_load(self.args)
        except Exception, e:
            self.exception = traceback.format_exc()

def main():
    args = parse_args()
    workers = []
    for i in xrange(NUM_WORKERS):
        worker = worker_thread(args)
        workers.append(worker)

    worker_failed = False
    for w in workers:
        w.join()
        if w.exception:
            print "worker hit an exception:\n%s\n'" % w.exception
            worker_failed = True

    if worker_failed:
        sys.exit(1)

if __name__ == '__main__':
  main()
