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



# Suggested use:
# This config file can be used with combinations.pl for running the optimizer grammars.


$combinations=
[
 ['
  --seed=random
  --threads=1
  --duration=3600
  --querytimeout=60
  --reporter=Shutdown,Backtrace,QueryTimeout,ErrorLog,ErrorLogAlarm
  --mysqld=--log-output=none
  --mysqld=--sql_mode=ONLY_FULL_GROUP_BY
  --sqltrace'
 ],[
  '',
  '--views',
  '--validator=Transformer',
  '--notnull',
  '--views --notnull --validator=Transformer'
 ],[
  '--basedir=/mysql/mysql-5.6.4-m6-linux-x86_64-debug',
  '--basedir=/mysql/mysql-5.6.4-m6-linux-x86_64-valgrind --reporter=ValgrindErrors --valgrind --validator=MarkErrorLog'
 ],[
  '--grammar=/randgen/conf/optimizer/outer_join.yy           --gendata=/randgen/conf/optimizer/outer_join.zz',
  '--grammar=/randgen/conf/optimizer/range_access.yy         --gendata=/randgen/conf/optimizer/range_access.zz',
  '--grammar=/randgen/conf/optimizer/optimizer_access_exp.yy --gendata=/randgen/conf/optimizer/range_access.zz',
  '--engine=INNODB --grammar=/randgen/conf/optimizer/optimizer_subquery.yy',
  '--engine=MYISAM --grammar=/randgen/conf/optimizer/optimizer_subquery.yy',
  '--engine=MEMORY --grammar=/randgen/conf/optimizer/optimizer_subquery.yy               --mysqld=--max_heap_table_size=268435456',
  '--engine=INNODB --grammar=/randgen/conf/optimizer/optimizer_no_subquery.yy',
  '--engine=MYISAM --grammar=/randgen/conf/optimizer/optimizer_no_subquery.yy',
  '--engine=MEMORY --grammar=/randgen/conf/optimizer/optimizer_no_subquery.yy            --mysqld=--max_heap_table_size=268435456',
  '--engine=INNODB --grammar=/randgen/conf/optimizer/optimizer_subquery_portable.yy',
  '--engine=MYISAM --grammar=/randgen/conf/optimizer/optimizer_subquery_portable.yy',
  '--engine=MEMORY --grammar=/randgen/conf/optimizer/optimizer_subquery_portable.yy      --mysqld=--max_heap_table_size=268435456',
  '--engine=INNODB --grammar=/randgen/conf/optimizer/optimizer_no_subquery_portable.yy',
  '--engine=MYISAM --grammar=/randgen/conf/optimizer/optimizer_no_subquery_portable.yy',
  '--engine=MEMORY --grammar=/randgen/conf/optimizer/optimizer_no_subquery_portable.yy   --mysqld=--max_heap_table_size=268435456',
  '--engine=INNODB --grammar=/randgen/conf/optimizer/archive/subquery_materialization.yy',
  '--engine=MYISAM --grammar=/randgen/conf/optimizer/archive/subquery_materialization.yy',
  '--engine=MEMORY --grammar=/randgen/conf/optimizer/archive/subquery_materialization.yy --mysqld=--max_heap_table_size=268435456',
  '--engine=INNODB --grammar=/randgen/conf/optimizer/archive/subquery_semijoin_nested.yy',
  '--engine=MYISAM --grammar=/randgen/conf/optimizer/archive/subquery_semijoin_nested.yy',
  '--engine=MEMORY --grammar=/randgen/conf/optimizer/archive/subquery_semijoin_nested.yy --mysqld=--max_heap_table_size=268435456',
  '--engine=INNODB --grammar=/randgen/conf/optimizer/archive/subquery_semijoin.yy',
  '--engine=MYISAM --grammar=/randgen/conf/optimizer/archive/subquery_semijoin.yy',
  '--engine=MEMORY --grammar=/randgen/conf/optimizer/archive/subquery_semijoin.yy        --mysqld=--max_heap_table_size=268435456'
 ]
];
