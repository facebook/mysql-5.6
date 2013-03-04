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

query:
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
	CACHE INDEX table_name /*!50400 PARTITION ( ALL ) */ IN cache_name |
	CACHE INDEX table_name /*!50400 PARTITION ( partition_name_list ) */ IN cache_name ;

load_index:
	LOAD INDEX INTO CACHE table_name /*!50400 PARTITION ( ALL ) */ ignore_leaves |
	LOAD INDEX INTO CACHE table_name /*!50400 PARTITION ( partition_name_list ) */ ignore_leaves ;

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

select:
	SELECT `col_int_nokey` % 10 AS `col_int_nokey` , `col_int_key` % 10 AS `col_int_key` FROM _table where ;

where:
	| | WHERE _field sign value ;

sign:
	> | < | = | <> | != | >= | <= ;

insert:
	insert_replace INTO table_name ( `col_int_nokey`, `col_int_key` ) VALUES ( value , value ) , ( value , value ) |
	insert_replace INTO table_name ( `col_int_nokey`, `col_int_key` ) select LIMIT _digit ;

insert_replace:
	INSERT | REPLACE ;

update:
	UPDATE table_name SET _field = value WHERE _field = value ;

delete:
	DELETE FROM table_name WHERE _field = value LIMIT _digit ;

_field:
	`col_int_nokey` | `col_int_nokey` ;

table_name:
	_letter | _table ;

value:
	_digit ;

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
	DROP PARTITION partition_name_list |
	partition |
	ADD PARTITION (PARTITION p3 VALUES LESS THAN MAXVALUE) |
	COALESCE PARTITION one_two | 
	REORGANIZE PARTITION |
	ANALYZE PARTITION partition_name_list |
	CHECK PARTITION partition_name_list |
	OPTIMIZE PARTITION partition_name_list |
	REBUILD PARTITION partition_name_list |
	REPAIR PARTITION partition_name_list |
	TRUNCATE PARTITION partition_name_list |
	REMOVE PARTITIONING;

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
	SUBPARTITION BY linear HASH ( _field ) SUBPARTITIONS _digit ;

partition_by_range:
	populate_ranges PARTITION BY RANGE ( _field ) subpartition (
		PARTITION p0 VALUES LESS THAN ( shift_range ),
		PARTITION p1 VALUES LESS THAN ( shift_range ),
		PARTITION p2 VALUES LESS THAN ( shift_range ),
		PARTITION p3 VALUES LESS THAN MAXVALUE
	);

populate_ranges:
	{ @ranges = sort { $a <=> $b } ($prng->digit(), $prng->fieldType('tinyint_unsigned'), $prng->fieldType('smallint_unsigned')) ; return undef } ;

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
	PARTITION BY linear HASH ( _field ) PARTITIONS _digit;

linear:
	| LINEAR;

partition_by_key:
	PARTITION BY KEY(`col_int_key`) PARTITIONS _digit ;

partition_item:
	PARTITION partition_name VALUES 


	PARTITION BY partition_hash_or_key;

partition_hash_or_key:
	HASH ( field_name ) partitions |
	KEY ( field_name ) partitions ;

partitions:
	PARTITIONS digit ;

_digit:
	1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 ;
