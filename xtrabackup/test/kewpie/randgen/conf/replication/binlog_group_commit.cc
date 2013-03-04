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
#

$combinations = [
	[
		'-queries=1M --duration=300 --reporter=BinlogCommitStats --mysqld=--loose-innodb-flush-log-at-trx-commit=2'
	],[
		'--threads=5',
		'--threads=10',
		'--threads=20',
		'--threads=30',
		'--threads=40',
	],[
		'
			--grammar=conf/transactions/combinations.yy
			--gendata=conf/transactions/combinations.zz
		','
			--grammar=conf/transactions/transactions.yy
			--gendata=conf/transactions/transactions.zz
			--validators=DatabaseConsistency
		','
			--gendata=conf/transactions/transactions.zz
			--grammar=conf/transactions/repeatable_read.yy
			--validators=RepeatableRead
		','
			--grammar=conf/transactions/transaction_durability.yy
		','
			--gendata=conf/replication/replication-dml_data.zz
			--grammar=conf/replication/replication-dml_sql.yy
		'
	],[
		'--rpl_mode=row',
		'--rpl_mode=statement',
		'--rpl_mode=mixed',
	],[
		'--engine=InnoDB --validator=ExplicitRollback',
		'--engine=PBXT --validator=ExplicitRollback --mysqld=--pbxt-support-xa'
	],[
		'','','','',
		'--mysqld=--binlog-dbug_fsync_sleep=100000',
		'--mysqld=--binlog-dbug_fsync_sleep=1000000'
	],[
		'', '', '', '',
		'--mysqld=--binlog-row-event-max-size=4K',
		'--mysqld=--binlog-row-event-max-size=64K'
	],[
		'--mysqld=--sync_binlog=1',
		'--mysqld=--sync_binlog=1',
		'--mysqld=--sync_binlog=1',
		'--mysqld=--sync_binlog=1',
		'--mysqld=--sync_binlog=0',
		'--mysqld=--sync_binlog=10',
		'--mysqld=--sync_binlog=100'
	],[
		'', '', '', '',	
		'--mysqld=--max_binlog_cache_size=8K',
		'--mysqld=--max_binlog_cache_size=64K'
	],[
		'', '', '', '',
		'--mysqld=--max_binlog_size=8K',
		'--mysqld=--max_binlog_size=64K'
	],[
		'', '', '', '',
		'--mysqld=--max_relay_log_size=4K',
		'--mysqld=--max_relay_log_size=64K',
	]
];
