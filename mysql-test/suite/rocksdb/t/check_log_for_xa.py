import sys
import re

"""
Example usage:
    python3 check_log_for_xa.py path/to/log/mysqld.2.err rollback,commit,prepare
"""

log_path = sys.argv[1]
desired_filters = sys.argv[2]

all_filters = [
  ('rollback', re.compile('(\[Server\] Rolling back XID:.+)')),
  ('commit', re.compile('(\[Server\] Committing XID:.+)')),
  ('prepare',
    re.compile('(\[Server\] Found \d+ prepared transaction\(s\) in \w+)')),
]

active_filters = [f for f in all_filters if f[0] in desired_filters]

with open(log_path) as log:
  for line in log:
    line = line.strip()
    for f in active_filters:
      match = f[1].search(line)
      if match:
        print("**found '%s' log entry**" % f[0])
