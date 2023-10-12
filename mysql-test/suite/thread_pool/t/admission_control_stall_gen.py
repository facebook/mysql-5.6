#!/usr/bin/python3

"""
Stall generator helper script for MTR.
"""
import time
import sys
import MySQLdb
from MySQLdb.constants import *
import argparse
import random
import threading
import traceback
import enum

def parse_args():
    parser = argparse.ArgumentParser(
        description='Generate queries that could cause CPU scheduler stalls',
    )
    parser.add_argument(
        '--host',
        default='127.0.0.1',
        type=str,
        help='host name')
    parser.add_argument(
        '-p',
        '--port',
        default=13010,
        type=int,
        help='port number')
    parser.add_argument(
        '-u',
        '--user',
        default='root',
        type=str,
        help='user name')
    parser.add_argument(
        '-d',
        '--database',
        default='test',
        type=str,
        help='database to use')
    parser.add_argument(
        '-n',
        '--num-tables',
        default=100,
        type=int,
        help='number of tables to create')
    return parser.parse_args()

def create_tables(con, num_tables):
    cursor = con.cursor()

    print(f"creating {num_tables} tables in single batch")
    ddl_list = [f"create table t{i} (a int);" for i in range(num_tables)]
    final_ddl = " ".join(ddl_list)
    cursor.execute(final_ddl)

    cursor.close()

def generate_load(args):
    con = MySQLdb.connect(user=args.user,
                          host=args.host,
                          port=args.port,
                          db=args.database)
    start = time.time()
    create_tables(con, args.num_tables)
    end = time.time()
    print(f"create tables took {end - start} seconds")
    con.close()

def main():
    args = parse_args()
    generate_load(args);

if __name__ == '__main__':
  main()
