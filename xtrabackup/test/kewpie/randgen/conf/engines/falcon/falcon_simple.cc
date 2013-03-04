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
		--engine=Falcon
		--reporters=Deadlock,ErrorLog,Backtrace,Recovery,Shutdown
		--mysqld=--loose-falcon-lock-wait-timeout=1
		--mysqld=--loose-innodb-lock-wait-timeout=1
		--mysqld=--log-output=none
		--mysqld=--loose-skip-safemalloc
	'],
	[
		'--mysqld=--transaction-isolation=READ-UNCOMMITTED',
		'--mysqld=--transaction-isolation=READ-COMMITTED',
		'--mysqld=--transaction-isolation=REPEATABLE-READ',
		'--mysqld=--transaction-isolation=SERIALIZABLE'
	],
	[
		'--mysqld=--falcon-page-size=2K',
		'--mysqld=--falcon-page-size=4K',
		'--mysqld=--falcon-page-size=8K',
		'--mysqld=--falcon-page-size=16K',
		'--mysqld=--falcon-page-size=32K'
	],
	[
		'--mem',
		''
	],
	[
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
		'--threads=64'
	],
	[
		'--mysqld=--falcon-checkpoint-schedule=\'1 1 1 1 1\'',
		'--mysqld=--falcon-checkpoint-schedule=\'1 * * * *\'',
		'--mysqld=--falcon-consistent=read=1',
		'--mysqld=--falcon-gopher-threads=1',
		'--mysqld=--falcon-index-chill-threshold=1',
		'--mysqld=--falcon-record-chill-threshold=1',
		'--mysqld=--falcon-io-threads=1',
		'--mysqld=--falcon-page-cache-size=1K',
#		'--mysqld=--falcon-record-memory-max=3M',
		'--mysqld=--falcon-scavenge-schedule=\'1 1 1 1 1\'',
		'--mysqld=--falcon-scavenge-schedule=\'1 * * * *\'',
		'--mysqld=--falcon-serial-log-buffers=1',
		'--mysqld=--falcon-use-deferred-index-hash=1',
		'--mysqld=--falcon-use-supernodes=0'
	]
];
