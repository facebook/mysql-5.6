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

# Other run options
#  '--basedir=/ssd/Percona-Server-5.5.28-rel29.3-432.Linux.x86_64'
#  '--basedir=/ssd/Percona-Server-5.5.28-rel29.3-432-debug-valgrind.Linux.x86_64
#    --valgrind --reporter=ValgrindErrors --validator=MarkErrorLog'

$combinations=
[
 ['
  --seed=random --duration=180 --querytimeout=60 --short_column_names
  --reporter=Shutdown,Backtrace,QueryTimeout,ErrorLog,ErrorLogAlarm
  --mysqld=--log-output=none --mysqld=--sql_mode=ONLY_FULL_GROUP_BY
  --grammar=conf/percona_qa/percona_qa.yy --gendata=conf/percona_qa/percona_qa.zz'
 ],[
  '--basedir=/ssd/Percona-Server-5.5.28-rel29.3-432-debug.Linux.x86_64'
 ],[
  '--threads=1',
  '--threads=25'
 ],[
  '--views',
  '--views --notnull',
  '--validator=Transformer',
  '--notnull --validator=Transformer',
  '--views --validator=Transformer',
  '--views --notnull --validator=Transformer'
 ],[
  '--mysqld=--innodb_track_changed_pages=0',
  '--mysqld=--innodb_track_changed_pages=1 --mysqld=--innodb_max_bitmap_file_size=9223372036854775807',
  '--mysqld=--innodb_track_changed_pages=1 --mysqld=--innodb_max_bitmap_file_size=20480'
 ],[
  '--mysqld=--innodb_changed_pages=OFF',
  '--mysqld=--innodb_changed_pages=ON --mysqld=--innodb_max_changed_pages=2',
  '--mysqld=--innodb_changed_pages=ON --mysqld=--innodb_max_changed_pages=100',
  '--mysqld=--innodb_changed_pages=FORCE --mysqld=--innodb_max_changed_pages=0',
  '--mysqld=--innodb_changed_pages=FORCE --mysqld=--innodb_max_changed_pages=1000'
 ],[
  '',
  '--mysqld=--innodb_log_file_size=1048576 --mysqld=--innodb_log_files_in_group=10 
    --mysqld=--innodb_log_buffer_size=10485761',
  '--mysqld=--innodb_log_file_size=10485761 --mysqld=--innodb_log_files_in_group=3 
    --mysqld=--innodb_log_buffer_size=1048577 --mysqld=--innodb_log_block_size=4096
    --mysqld=--innodb_fast_shutdown=0 --mysqld=--innodb_adaptive_flushing_method=keep_average
    --mysqld=--skip-innodb_doublewrite --mysqld=--userstat',
  '--mysqld=--innodb_log_file_size=1048576 --mysqld=--innodb_log_files_in_group=2 
    --mysqld=--innodb_log_buffer_size=1048576 --mysqld=--innodb_log_block_size=512
    --mysqld=--innodb_fast_shutdown=2 --mysqld=--innodb_adaptive_flushing_method=native
    --mysqld=--innodb_use_global_flush_log_at_trx_commit=0 --mysqld=--userstat'
 ],[
  '',
  '--mysqld=--innodb_flush_method=O_DSYNC',
  '--mysqld=--innodb_flush_method=O_DIRECT'
 ],[
  '',
  '--mysqld=--innodb_file_per_table=1',
  '--mysqld=--innodb_file_per_table=1 --mysqld=--innodb_file_format=barracuda'
 ]
]
