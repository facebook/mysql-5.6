# Copyright (C) 2008-2009 Sun Microsystems, Inc. All rights reserved.
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

query_init:
	create ; create ; create ; create ; create ; create ; create ; create ; create ; create ;

query:
	select_explain | select_explain | 
	select | select | select | select | select | select |
	select | select | select | select | select | select |
	select | select | select | select | select | select |

	insert | update | delete | insert | update | delete |
	insert | update | delete | insert | update | delete |
	insert | update | delete | insert | update | delete |

	create | create | drop | alter |
	cache_index | load_index |
	set_key_buffer_size | set_key_cache_block_size ;

cache_index:
	CACHE INDEX table_name IN cache_name |
	CACHE INDEX _letter /*!50400 PARTITION ( ALL ) */ IN cache_name |
	CACHE INDEX _letter /*!50400 PARTITION ( partition_name_list ) */ IN cache_name ;

load_index:
	LOAD INDEX INTO CACHE table_name ignore_leaves |
	LOAD INDEX INTO CACHE _letter /*!50400 PARTITION ( ALL ) */ ignore_leaves |
	LOAD INDEX INTO CACHE _letter /*!50400 PARTITION ( partition_name_list ) */ ignore_leaves ;

ignore_leaves:
	| IGNORE LEAVES ;

set_key_buffer_size:
	/*!50400 SET GLOBAL cache_name . key_buffer_size = _tinyint_unsigned  */ |
	/*!50400 SET GLOBAL cache_name . key_buffer_size = _smallint_unsigned */ |
	/*!50400 SET GLOBAL cache_name . key_buffer_size = _mediumint_unsigned */ ;

set_key_cache_block_size:
	/*!50400 SET GLOBAL key_cache_block_size = key_cache_block_size_enum */ ;

key_cache_block_size_enum:
	512 | 1024 | 2048 | 4096 | 8192 | 16384 ;	
		
cache_name:
	c1 | c2 | c3 | c4;

select_explain:
	EXPLAIN /*!50100 PARTITIONS */ SELECT _field FROM table_name where ;

select:
	SELECT `col_int_nokey` % 10 AS `col_int_nokey` , `col_int_key` % 10 AS `col_int_key` FROM table_name where ;

# WHERE clauses suitable for partition pruning
where:
	| |
	WHERE _field comparison_operator value |
	WHERE _field BETWEEN value AND value ;

comparison_operator:
	> | < | = | <> | != | >= | <= ;

insert:
	insert_replace INTO table_name ( `col_int_nokey`, `col_int_key` ) VALUES ( value , value ) , ( value , value ) |
	insert_replace INTO table_name ( `col_int_nokey`, `col_int_key` ) select ORDER BY `col_int_key` , `col_int_nokey` LIMIT limit_rows ;

insert_replace:
	INSERT | REPLACE ;

update:
	UPDATE table_name SET _field = value WHERE _field = value ;

delete:
	DELETE FROM table_name WHERE _field = value ORDER BY `col_int_key` , `col_int_nokey` LIMIT limit_rows ;

_field:
	`col_int_nokey` | `col_int_nokey` ;

table_name:
	_letter | _table ;

value:
	_digit ;

# We can not use IF NOT EXISTS here to reduce the "Table doesn't exist errors", because
# If we run the same grammar on 5.0, the CREATE will always succeed, but in 5.1/5.4 it 
# can still fail due to a partition type mismatch

create:
	CREATE TABLE _letter (
		`col_int_nokey` INTEGER,
		`col_int_key` INTEGER NOT NULL,
		KEY (`col_int_key`)
	) ENGINE = engine /*!50100 partition */ select ;

drop:
	DROP TABLE IF EXISTS _letter ;

alter:
	/*!50400 ALTER TABLE _letter alter_operation */;

alter_operation:
	ENGINE = engine |
	enable_disable KEYS |
	ORDER BY _field |
	partition |
	ADD PARTITION (PARTITION p3 VALUES LESS THAN MAXVALUE) |
	ADD PARTITION (PARTITION p3 VALUES LESS THAN MAXVALUE) |
	COALESCE PARTITION one_two | 
	REORGANIZE PARTITION |
	ANALYZE PARTITION partition_name_list |
	CHECK PARTITION partition_name_list |
	REBUILD PARTITION partition_name_list |
	REPAIR PARTITION partition_name_list |
	OPTIMIZE PARTITION partition_name_list |	# bug47459
	REMOVE PARTITIONING |				# bug42438
	DROP PARTITION partition_name_list |		# DROP and TRUNCATE 
	TRUNCATE PARTITION partition_name_list		# can not be used in comparison tests against 5.0
;

one_two:
	1 | 2;

partition_name_list:
	partition_name |
	partition_name |
	partition_name |
	partition_name_list;

partition_name:
	p0 | p1 | p2 | p3 ;

enable_disable:
	ENABLE | DISABLE ;

# Give preference to MyISAM because key caching is specific to MyISAM

engine:
	MYISAM | MYISAM | MYISAM |
	INNODB | MEMORY ;

partition:
	|
	partition_by_range |
	partition_by_list |
	partition_by_hash |
	partition_by_key
;

subpartition:
	|
	SUBPARTITION BY linear HASH ( _field ) SUBPARTITIONS partition_count ;

partition_by_range:
	populate_ranges PARTITION BY RANGE ( _field ) subpartition (
		PARTITION p0 VALUES LESS THAN ( shift_range ),
		PARTITION p1 VALUES LESS THAN ( shift_range ),
		PARTITION p2 VALUES LESS THAN ( shift_range ),
		PARTITION p3 VALUES LESS THAN MAXVALUE
	);

populate_ranges:
	{ @ranges = ($prng->digit(), $prng->int(10,255), $prng->int(256,65535)) ; return undef } ;

shift_range:
	{ shift @ranges };

partition_by_list:
	populate_digits PARTITION BY LIST ( _field ) subpartition (
		PARTITION p0 VALUES IN ( shift_digit, NULL ),
		PARTITION p1 VALUES IN ( shift_digit, shift_digit, shift_digit ),
		PARTITION p2 VALUES IN ( shift_digit, shift_digit, shift_digit ),
		PARTITION p3 VALUES IN ( shift_digit, shift_digit, shift_digit )
	);

populate_digits:
	{ @digits = @{$prng->shuffleArray([0..9])} ; return undef };

shift_digit:
	{ shift @digits };

partition_by_hash:
	PARTITION BY linear HASH ( _field ) PARTITIONS partition_count;

linear:
	| LINEAR;

partition_by_key:
	PARTITION BY KEY(`col_int_key`) PARTITIONS partition_count ;

partition_item:
	PARTITION partition_name VALUES 


	PARTITION BY partition_hash_or_key;

partition_hash_or_key:
	HASH ( field_name ) PARTITIONS partition_count |
	KEY ( field_name ) PARTITIONS partition_count ;

limit_rows:
	1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 ;

partition_count:
	1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 ;
