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

# --views is not used ftm as it causes too many STATUS_PERL_FAILURE(255):
# Table/view 'table0_int' in schema 'test' has no 'indexed' columns (Might 
# be caused by use of --views option in combination with grammars containing 
# _field_indexed) at /ssd/randgen//lib/GenTest/Generator/FromGrammar.pm line 290
# Even if it is not used, this error is shown (though much less), and this is
# likely due to '_field_indexed' being used, even if --views is not used.

$combinations=
[
 [
  '--seed=random --duration=240 --querytimeout=60
   --reporter=Shutdown,Backtrace,QueryTimeout,ErrorLog,ErrorLogAlarm
   --mysqld=--log-output=none --mysqld=--sql_mode=ONLY_FULL_GROUP_BY
   --mysqld=--slow_query_log --mysqld=--userstat 
   --mysqld=--innodb_track_changed_pages=1 --mysqld=--innodb_changed_pages=ON
   --mysqld=--innodb_log_archive=1 --mysqld=--thread_handling=pool-of-threads
   --mysqld="--plugin-load=audit_log=audit_log.so;tokudb=ha_tokudb.so"
   --mysqld=--init-file='.$ENV{RQG_HOME}.'/conf/percona_qa/5.6/TokuDB.sql'
 ],[
  '--threads=1',
  '--threads=25'
 ],[
  '--mysqld=--innodb_file_per_table=1 --mysqld=--innodb_file_format=barracuda --mysqld=--innodb_log_files_in_group=3',
  '--notnull --mysqld=--innodb_flush_method=O_DSYNC --mysqld=--innodb-buffer-pool-populate --mysqld=--innodb_log_block_size=512
   --mysqld=--innodb_fast_shutdown=0 --mysqld=--skip-innodb_doublewrite --mysqld=--innodb_flush_log_at_trx_commit=2',
  '--mysqld=--innodb_file_per_table=1 --validator=Transformer --mysqld=--innodb_log_arch_dir=_epoch',
  '--mysqld=--innodb_flush_method=O_DIRECT --mysqld=--innodb_use_global_flush_log_at_trx_commit=0
   --mysqld=--enforce-storage-engine=InnoDB --mysqld=--query_cache_type=1 --mysqld=--query_cache_size=1048576'
 ],[
  '--basedir=PERCONA-DBG-SERVER',
  '--basedir=PERCONA-VAL-SERVER
   --valgrind --reporter=ValgrindErrors --validator=MarkErrorLog'
 ],[
#GRAMMAR-GENDATA-DUMMY-TAG   # do not remove, and leave file otherwise as-is, except you may make modifications above as needed
