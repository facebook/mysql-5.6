# Copyright (C) 2008-2010 Sun Microsystems, Inc. All rights reserved.
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

$combinations = [
	[
	'
		--grammar=conf/engines/engine_stress.yy
		--gendata=conf/engines/engine_stress.zz
		--engine=Innodb
		--reporters=Deadlock,ErrorLog,Backtrace,Recovery,Shutdown
		--mysqld=--loose-falcon-lock-wait-timeout=1
		--mysqld=--loose-innodb-lock-wait-timeout=1
		--mysqld=--log-output=none
		--mysqld=--loose-skip-safemalloc
		--mem
		--duration=1200
	'], [
		'--mysqld=--loose-innodb-thread-concurrency-timer-based=TRUE',
		'--mysqld=--loose-innodb-thread-concurrency-timer-based=FALSE'
	], [
		'--rows=10',
		'--rows=100',
		'--rows=1000',
		'--rows=10000'
	],
	[
		'--threads=4',
		'--threads=8',
		'--threads=16',
		'--threads=32',
		'--threads=64',
		'--threads=128'
	],[
		'--mysqld=--skip-innodb-adaptive-hash-index',
		'--mysqld=--innodb-autoinc-lock-mode=0',
		'--mysqld=--innodb-autoinc-lock-mode=2',
		'--mysqld=--skip-innodb-checksums',
		'--mysqld=--innodb-commit-concurrency=1',
		'--mysqld=--innodb-concurrency-tickets=100',
		'--mysqld=--skip-innodb-doublewrite',
		'--mysqld=--innodb-flush-log-at-trx-commit=0',
		'--mysqld=--innodb-flush-log-at-trx-commit=2',
		'--mysqld=--innodb-log-buffer-size=8M',
		'--mysqld=--innodb-max-purge-lag=1M',
		'--mysqld=--innodb-sync-spin-loops=1',
		'--mysqld=--innodb-thread-concurrency=1',
		'--mysqld=--innodb-thread-concurrency=1000',
		'--mysqld=--innodb-thread-sleep-delay=0'
	],
        [
                '--mysqld=--transaction-isolation=READ-UNCOMMITTED',
                '--mysqld=--transaction-isolation=READ-COMMITTED',
                '--mysqld=--transaction-isolation=REPEATABLE-READ',
                '--mysqld=--transaction-isolation=SERIALIZABLE'
        ],

];
