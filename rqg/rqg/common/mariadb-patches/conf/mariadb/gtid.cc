# Copyright (C) 2013 Monty Program Ab
# 
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.


$combinations = [
	[
	'
		--no-mask
		--seed=time
		--threads=8
		--duration=200
		--queries=100M
		--reporters=QueryTimeout,Backtrace,ErrorLog,Deadlock,MariadbGtidCrashSafety
		--engine=InnoDB
		--mysqld=--log_bin=mysql-bin
		--mysqld=--innodb_lock_wait_timeout=2
		--mysqld=--lock_wait_timeout=5
		--mysqld=--log_output=FILE
		--mysqld=--ignore_builtin_innodb
		--mysqld=--plugin_load=ha_innodb.so
	'], 
	[
		'--grammar=conf/mariadb/gtid_stress.yy --gendata=conf/mariadb/gtid_stress.zz'
	],
	[
		'--rpl_mode=row --mysqld=--binlog_format=row',
		'--rpl_mode=statement --mysqld=--binlog_format=statement',
		'--rpl_mode=mixed --mysqld=--binlog_format=mixed'
	],
	[
		'--mysqld=--gtid_strict_mode=1',
		'--mysqld=--gtid_strict_mode=0'
	],
	[
		'--mysqld=--log_slave_updates',
		'--mysqld=--skip_log_slave_updates'
	],
	[
		'--mysqld=--max_binlog_size=4096',
		''
	],
	[
		'--mysqld=--sync_binlog=1',
		'--mysqld=--sync_binlog=0'
	],
	[
		'--mysqld=--slave_compressed_protocol=1',
		'--mysqld=--slave_compressed_protocol=0'
	],
	[
		'--mysqld=--binlog_annotate_row_events=1',
		'--mysqld=--binlog_annotate_row_events=0'
	],
	[
		'--mysqld=--replicate_annotate_row_events=1',
		'--mysqld=--replicate_annotate_row_events=0'
	],
	[
		'--mysqld=--binlog-checksum=CRC32',
		'--mysqld=--binlog-checksum=NONE'
	],
	[
		'--mysqld=--binlog_optimize_thread_scheduling=OFF',
		'--mysqld=--binlog_optimize_thread_scheduling=ON'
	],
	[
		'--mysqld=--relay_log_purge=1',
		'--mysqld=--relay_log_purge=0',
	]
];

