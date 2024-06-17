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
  --mysqld=--log-output=none --mysqld=--sql_mode=ONLY_FULL_GROUP_BY
  --mysqld=--slow_query_log'
 ],[
  '--grammar=conf/percona_qa/5.6/5.6.yy --gendata=conf/percona_qa/5.6/5.6.zz1',
  '--grammar=conf/percona_qa/5.6/5.6.yy --gendata=conf/percona_qa/5.6/5.6.zz2',
  '--grammar=conf/percona_qa/5.6/5.6.yy --gendata=conf/percona_qa/5.6/5.6.zz3'
 ],[
  '--views --notnull --validator=Transformer',
  '--views --validator=Transformer',
  '--notnull --validator=Transformer',
  '--validator=Transformer',
  '--views --notnull',
  '--views'
 ],[
  '--basedir=/ssd/mysql-5.6.12-linux-x86_64-debug',
  '--basedir=/ssd/mysql-5.6.12-linux-x86_64-debug-valgrind
   --valgrind --reporter=ValgrindErrors --validator=MarkErrorLog'
 ],[
  '--threads=25',
  '--threads=1'
 ],[
  '--no-mask --mysqld=--innodb_file_per_table=1',
  '--no-mask --mysqld=--innodb_file_per_table=1 --mysqld=--innodb_file_format=barracuda',
  '--mask-level=1',
  ''
 ],[
  '--mysqld=--innodb_flush_method=O_DSYNC',
  '--mysqld=--innodb_flush_method=O_DIRECT',
  ''
 ],[
  '--mysqld=--innodb_log_file_size=1048576 --mysqld=--innodb_log_files_in_group=2
    --mysqld=--innodb_log_buffer_size=1048576 
    --mysqld=--innodb_fast_shutdown=2 --mysqld=--innodb_log_group_home_dir=_epoch',
  '--mysqld=--innodb_log_file_size=1048576 --mysqld=--innodb_log_files_in_group=10
    --mysqld=--innodb_log_buffer_size=10485761 --mysqld=--innodb_flush_log_at_trx_commit=2 
    --mysqld=--query_cache_type=1 --mysqld=--query_cache_size=1048576',
  '--mysqld=--innodb_log_file_size=10485761 --mysqld=--innodb_log_files_in_group=3
    --mysqld=--innodb_log_buffer_size=1048577 
    --mysqld=--innodb_fast_shutdown=0 
    --mysqld=--skip-innodb_doublewrite --mysqld=--secure-file-priv=/tmp'
 ]
]
