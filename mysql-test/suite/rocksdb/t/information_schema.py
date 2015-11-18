import sys, re
"""
example call:
    python information_schema.py cf_id:0,index_id:123 123
    stdout: Max dropped index id does reflect the newest index being dropped
"""

reg = 'cf_id:[0-9]+,index_id:([0-9]+)'
drop_index_id = int(re.search(reg, sys.argv[1]).group(1))
newest_index_id = int(sys.argv[2])

if drop_index_id != newest_index_id:
    print 'FAIL with args: %s' % str(sys.argv)
else:
    print 'Max dropped index id reflect the newest index being dropped'
