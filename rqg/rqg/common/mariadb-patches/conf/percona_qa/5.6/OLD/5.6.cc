# Copyright (c) 2012 Oracle and/or its affiliates. All rights reserved.
# Use is subject to license terms.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301
# USA

# Certain parts (c) Percona Inc

# Not ported yet ftm (listed just after innodb_fast_shutdown)
# --mysqld=--innodb_adaptive_flushing_method=native
# --mysqld=--innodb_adaptive_flushing_method=keep_average

$combinations=
[
 ['
  --seed=random --duration=300 --querytimeout=60 --short_column_names
  --reporter=Shutdown,Backtrace,QueryTimeout,ErrorLog,ErrorLogAlarm
  --mysqld=--log-output=none --mysqld=--sql_mode=ONLY_FULL_GROUP_BY'
 ],[
  '--grammar=conf/percona_qa/5.6/5.6.yy --gendata=conf/percona_qa/5.6/5.6.zz',
  '--grammar=conf/percona_qa/5.6/5.6.yy --gendata=conf/percona_qa/5.6/5.6.zz2',
  '--grammar=conf/percona_qa/5.6/5.6.yy --gendata=conf/percona_qa/5.6/5.6.zz3'
 ],[
  '--basedir=/Percona-Server',
  '--basedir=/Percona-Server --valgrind --reporter=ValgrindErrors --validator=MarkErrorLog'
 ],[
  '--threads=25',
  '--threads=1'
 ],[
  '--no-mask --mysqld=--innodb_file_per_table=1',
  '--no-mask --mysqld=--innodb_file_per_table=1 --mysqld=--innodb_file_format=barracuda',
  '--mask-level=1',
  ''
 ],[
  '--mysqld=--innodb_flush_method=O_DSYNC
    --mysqld=--minimum-join-buffer-size=128 --mysqld=--readonly-loose-max-connect-errors=1
    --mysqld=--readonly-key-cache-block-size=1 --mysqld=--hidden-key-buffer-size=1
    --mysqld=--loose-readonly-key-cache-division-limit=1',
  '--mysqld=--innodb_flush_method=O_DIRECT
    --mysqld=--hidden-key-buffer-size=0 --mysqld=--loose-readonly-key-cache-division-limit=0
    --mysqld=--readonly-loose-max-connect-errors=0 --mysqld=--readonly-key-cache-block-size=0',
  ''
 ],[
  '--views --notnull --validator=Transformer',
  '--views --validator=Transformer',
  '--notnull --validator=Transformer',
  '--validator=Transformer',
  '--views --notnull',
  '--views'
 ],[
  '--mysqld=--innodb_track_changed_pages=1 --mysqld=--innodb_max_bitmap_file_size=4097
   --mysqld=--innodb_changed_pages=ON --mysqld=--innodb_max_changed_pages=2
   --mysqld=--slow_query_log --mysqld=--userstat --mysqld=--innodb-buffer-pool-populate
   --mysqld=--innodb_log_archive=1 --mysqld=--innodb_log_arch_dir=_epoch
   --mysqld=--innodb_log_arch_expire_sec=120',
  '--mysqld=--innodb_track_changed_pages=1 --mysqld=--innodb_max_bitmap_file_size=20480
   --mysqld=--innodb_changed_pages=ON --mysqld=--innodb_max_changed_pages=0',
  '--mysqld=--innodb_track_changed_pages=1 --mysqld=--innodb_max_bitmap_file_size=9223372036854775807
   --mysqld=--innodb_changed_pages=FORCE --mysqld=--innodb_max_changed_pages=100',
  '--mysqld=--innodb_track_changed_pages=0 --mysqld=--innodb_changed_pages=FORCE
   --mysqld=--slow_query_log --mysqld=--userstat --mysqld=--thread_handling=pool-of-threads',
  '--mysqld=--thread_handling=pool-of-threads --mysqld=--userstat'
 ],[
  '--mysqld=--innodb_log_file_size=1048576 --mysqld=--innodb_log_files_in_group=2
   --mysqld=--innodb_log_buffer_size=1048576 --mysqld=--innodb_log_block_size=512
   --mysqld=--innodb_fast_shutdown=2 --mysqld=--innodb_log_group_home_dir=_epoch
   --mysqld=--innodb_use_global_flush_log_at_trx_commit=0',
  '--mysqld=--innodb_log_file_size=1048576 --mysqld=--innodb_log_files_in_group=10
   --mysqld=--innodb_log_buffer_size=10485761 --mysqld=--innodb_flush_log_at_trx_commit=2 
   --mysqld=--query_cache_type=1 --mysqld=--query_cache_size=1048576',
  '--mysqld=--innodb_log_file_size=10485761 --mysqld=--innodb_log_files_in_group=3
   --mysqld=--innodb_log_buffer_size=1048577 --mysqld=--innodb_log_block_size=4096
   --mysqld=--innodb_fast_shutdown=0 
   --mysqld=--skip-innodb_doublewrite',
  '--mysqld=--enforce-storage-engine=InnoDB --mysqld=--utility-user=roel 
   --mysqld=--utility-user-password=test --mysqld=--secure-file-priv=/tmp
   --mysqld=--utility-user-schema-access=mysql,information_schema'
 ]
]
