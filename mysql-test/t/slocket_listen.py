#!/bin/env python
from socket import socket, AF_UNIX, SOCK_DGRAM
from select import select
from os import unlink, getcwd, stat
from os.path import exists
from os.path import relpath
from sys import argv, exit


def main():
  if len(argv) < 2:
    print 'Usage: %s <socket name>' % argv[0]
    exit(1)

  sn = relpath(argv[1] + 'slocket', getcwd())

  s = socket(AF_UNIX, SOCK_DGRAM)

  s.setblocking(False)

  try_unlink(sn)
  s.bind(sn)

  print 'listening on %r' % sn

  while not exists(relpath(argv[1] + '/slocket_listen_kill_flag', getcwd())):
    x,_,_ = select([s], [], [], 1.0)
    if len(x) > 0:
      x = x[0]
      y = x.recv(1024)
      print y

  print 'found slocket_listen_kill_flag... closing'
  s.close()
  try_unlink(sn)

def try_unlink(sn):
  try:
    unlink(sn)
  except:
    pass

if __name__ == '__main__':
  main()
