# Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
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
# This grammar contains ddl for partitions as well as partition extentions to dml.
# It creates partitioned and not partitioned tables and also dml without partition
# extension. The focus is on partitions and dml with partition extention.
# The grammar doesn't rely on the predefined tables, but uses instead tables defined in
# query_init.
#
##########################################################################
# Initialization of tables with focus on partitioned tables.

init_db:
	create_tables ; insert_tables ;  cache_index ;

##########################################################################
# Randomly executed SQL

exec_sql:
	select_explain |
	select | select | select | select | select | select                   |
	select | select | select | select | select | select                   |
	select | select | select | select | select | select                   |
	insert | update | delete | insert | update                            |
	insert | update | delete | insert | update                            |
	alter | alter | alter | alter | alter | alter                         |
	alter | alter | alter | alter | alter | alter                         |
	cache_index                                                           |
	create_sel | create_sel | create_sel | create_sel | create_sel | drop |
	set_key_buffer_size | set_key_cache_block_size                        ;

alter_operation:
	partition                                                           |
	enable_disable KEYS                                                 |
	ADD PARTITION (PARTITION partition_name VALUES LESS THAN MAXVALUE)  |
	ADD PARTITION (PARTITION p3 VALUES LESS THAN MAXVALUE)              |
	DROP PARTITION partition_name                                       |
	COALESCE PARTITION one_two                                          |
        EXCHANGE PARTITION partition_name WITH TABLE table_name_nopart      |
	ANALYZE PARTITION partition_name_list                               |
	CHECK PARTITION partition_name_list                                 |
	REBUILD PARTITION partition_name_list                               |
	REPAIR PARTITION partition_name_list                                |
	REMOVE PARTITIONING                                                 |   # bug42438
	TRUNCATE PARTITION partition_name_list		# can not be used in comparison tests against 5.0
;

#	OPTIMIZE PARTITION partition_name_list |	# bug47459
# Due to not complete syntax.
#	REORGANIZE PARTITION partition_name_list |
# Due to bug 11871889
#	ENGINE = engine |
# Due to bug 11872117
#	ORDER BY _field |

