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

# All 3 run options (opt,dbg,val)
#  '--basedir=/ssd/Percona-Server-5.6.8-alpha60.2-314.Linux.x86_64'
#  '--basedir=/ssd/Percona-Server-5.6.8-alpha60.2-314-debug.Linux.x86_64'
#  '--basedir=/ssd/Percona-Server-5.6.8-alpha60.2-314-valgrind.Linux.x86_64
#    --valgrind --reporter=ValgrindErrors --validator=MarkErrorLog'

# Not ported to 5.6 yet
# --mysqld=--innodb_log_block_size=4096
# --mysqld=--innodb_log_block_size=512
# --mysqld=--innodb_adaptive_flushing_method=keep_average
# --mysqld=--innodb_adaptive_flushing_method=native
# --mysqld=--innodb_use_global_flush_log_at_trx_commit=0
# --mysqld=--userstat

$combinations=
[
 ['
  --seed=random --duration=300 --querytimeout=60 --short_column_names
  --reporter=Shutdown,Backtrace,QueryTimeout,ErrorLog,ErrorLogAlarm
  --mysqld=--log-output=none --mysqld=--sql_mode=ONLY_FULL_GROUP_BY
  --grammar=conf/percona_qa/percona_qa.yy --gendata=conf/percona_qa/percona_qa.zz'
 ],[
  '--basedir=/ssd/Percona-Server-5.6.8-alpha60.2-314-debug.Linux.x86_64'
 ],[
  '--threads=25',
  '--threads=1'
 ],[
  '--validator=Transformer --views --notnull',
  '--validator=Transformer',
  '--views --notnull',
  ''
 ],[
  '--mysqld=--innodb_log_archive=1 --mysqld=--innodb_log_arch_dir=_epoch',
  '--mysqld=--innodb_log_archive=1',
  '--mysqld=--innodb_log_archive=0',
  ''
 ],[
  '--mysqld=--innodb_log_arch_expire_sec=10',
  '--mysqld=--innodb_log_arch_expire_sec=0',
  '--mysqld=--innodb_log_arch_expire_sec=1',
  '--mysqld=--innodb_log_arch_expire_sec=99999999',
  ''
 ],[
  '--mysqld=--innodb_log_file_size=1048576 --mysqld=--innodb_log_files_in_group=10 
    --mysqld=--innodb_log_buffer_size=10485761 --mysqld=--innodb_log_group_home_dir=_epoch
    --mysqld=--innodb_flush_log_at_trx_commit=2 --mysqld=--innodb_flush_log_at_timeout=0',
  '--mysqld=--innodb_log_file_size=10485761 --mysqld=--innodb_log_files_in_group=3 
    --mysqld=--innodb_log_buffer_size=1048577 --mysqld=--innodb_fast_shutdown=0 
    --mysqld=--skip-innodb_doublewrite --mysqld=--innodb_file_per_table=1
    --mysqld=--innodb_flush_log_at_trx_commit=2 --mysqld=--innodb_flush_log_at_timeout=1',
  '--mysqld=--innodb_log_file_size=1048576 --mysqld=--innodb_log_files_in_group=2 
    --mysqld=--innodb_log_buffer_size=262144 --mysqld=--innodb_fast_shutdown=2
    --mysqld=--innodb_flush_log_at_trx_commit=2 --mysqld=--innodb_flush_log_at_timeout=27000',
  '--mysqld=--innodb_flush_log_at_trx_commit=0 --mysqld=--innodb_flush_method=O_DSYNC
   --mysqld=--innodb_log_arch_dir=_epoch --mysqld=--innodb_log_group_home_dir=_epoch',
  '--mysqld=--innodb_flush_log_at_trx_commit=1 --mysqld=--innodb_flush_method=O_DIRECT
   --mysqld=--innodb_file_per_table=1 --mysqld=--innodb_file_format=barracuda',
 ]
]
