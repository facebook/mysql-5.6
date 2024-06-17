# Copyright (c) 2003, 2012, Oracle and/or its affiliates. All rights reserved.
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
# Experiences
#
# --rpl_mode=row:
# perl runall.pl  --engine=MyISAM --debug --rpl_mode=row --duration=12000 --queries=40000 
# --reporter=Deadlock,Backtrace,ErrorLog --threads=10 --basedir=/home/horst/bzrysql-trunk 
# --mysqld=--lock-wait-timeout=1 --mysqld=--general-log=ON --mysqld=--default-storage-engine=
# MyISAM --mysqld=--log-output=table --mysqld=--loose-innodb-lock-wait-timeout=1 
# --grammar=./conpartitioning/partitions.yy --vardir=/dev/shm/var1 --mask-level=0 
# --mask=0 --seed=1 >./storage/1.l 2>&1a
#
# Failed sometimes with reclication-error 103, but also succedded (needed about 5-10 Minutes). 
# Produced a log file of about 10MB and a var directory with 6,6MB.
# 
# --rpl_mode=mixed:
# Failed sometimes with reclication-error 103, but also succedded (needed about 5-10 Minutes). 
# Produced a log file of about 36KB  and a var directory with 5MB.
# 
# perl runall.pl  --engine=MyISAM --debug --duration=12000 --queries=40000 
# --reporter=Deadlock,Backtrace,ErrorLog --threads=10 
# --basedir=/home/horst/bzr/mysql-trunk--mysqld=--lock-wait-timeout=1 --mysqld=--general-log=ON 
# --mysqld=--default-storage-engine=MyISAM --mysqld=--log-output=table 
# --mysqld=--loose-innodb-lock-wait-timeout=1 --grammar=./conf/partitioning/partitions.yy 
# --vardir=/dev/shm/var1 --mask-level=0 --mask=0 --seed=1 >./storage/1.log 2>&1

# Log: 21MB, var: 148Mb, time about 8Min.  Semantic_error: 12234, OK:27980

# perl runall.pl  --engine=MyISAM --debug --duration=24000 --queries=100000 
# --reporter=Deadlock,Backtrace,ErrorLog --threads=16 
# --basedir=/home/horst/bzr/mysql-trunk--mysqld=--lock-wait-timeout=1 --mysqld=--general-log=ON 
# --mysqld=--default-storage-engine=MyISAM --mysqld=--log-output=table 
# --mysqld=--loose-innodb-lock-wait-timeout=1 --grammar=./conf/partitioning/partitions.yy 
# --vardir=/dev/shm/var1 --mask-level=0 --mask=0 --seed=1 >./storage/1.log 2>&1

# Log: 48MB, var: 320Mb, time about 20Min. 


##########################################################################
# Initialization of tables with focus on partitioned tables.

query_init:
	{our $nb_parts= 50; return undef } 
	init_db ;

init_db:
	create_tables ; insert_tables ;  cache_index ; load_index ;

create_tables:
	create_10 ; create_10 ; create_10 ; create_10 ; create_10 ; create_10 ; create_nop_4 ;

create_10:
	create ; create ; create ; create ; create ; create ; create ; create ; create ; create ;

create_nop_4:
	create_nop ; create_nop ; create_nop ; create_nop ;

insert_tables:
	insert_part_tables ; insert_part_tables ; insert_part_tables ; insert_part_tables ; insert_nop_tables ;

insert_part_tables:
	insert_part_6 ; insert_part_6 ; insert_part_6 ; insert_part_6 ; insert_part_6 ;

insert_nop_tables:
	insert_nop_6 ; insert_nop_6 ; insert_nop_6 ; insert_nop_6 ; insert_nop_6 ;

insert_part_6:
	insert_part ; insert_part ; insert_part ; insert_part ; insert_part ; insert_part ;

insert_nop_6:
	insert_nop ; insert_nop ; insert_nop ; insert_nop ; insert_nop ; insert_nop ;

create:
        CREATE TABLE if_not_exists table_name_part (
                `col_int_nokey` INTEGER,
                `col_int_key` INTEGER NOT NULL,
                KEY (`col_int_key`)
	) ENGINE = engine /*!50100 partition */ ;

create_nop:
        CREATE TABLE if_not_exists table_name_nopart (
                `col_int_nokey` INTEGER,
                `col_int_key` INTEGER NOT NULL,
                KEY (`col_int_key`)
	) ENGINE = engine ;

insert_part:
        INSERT INTO table_name_part   ( `col_int_nokey`, `col_int_key` ) VALUES ( value , value ) , ( value , value ) , ( value , value ) , ( value , value ) ;

insert_nop:
        INSERT INTO table_name_nopart ( `col_int_nokey`, `col_int_key` ) VALUES ( value , value ) , ( value , value ) , ( value , value ) , ( value , value ) ;

##########################################################################
# Randomly executed SQL

query:
	exec_sql ;

exec_sql:
	select_explain |
	select | select | select | select | select | select                   |
	select | select | select | select | select | select                   |
	select | select | select | select | select | select                   |
	insert | update | delete | insert | update                            |
	insert | update | delete | insert | update                            |
	alter | alter | alter | alter | alter | alter                         |
	alter | alter | alter | alter | alter | alter                         |
	cache_index | load_index                                              |
	create_sel | create_sel | create_sel | create_sel | create_sel | drop |
	set_key_buffer_size | set_key_cache_block_size                        ;

cache_index:
	CACHE INDEX table_name_letter IN cache_name                                               |
	CACHE INDEX table_name_letter /*!50400 PARTITION ( ALL ) */ IN cache_name                 |
	CACHE INDEX table_name_letter /*!50400 PARTITION ( partition_name_list ) */ IN cache_name ;

load_index:
	LOAD INDEX INTO CACHE table_name_letter ignore_leaves                                               |
	LOAD INDEX INTO CACHE table_name_letter /*!50400 PARTITION ( ALL ) */ ignore_leaves                 |
	LOAD INDEX INTO CACHE table_name_letter /*!50400 PARTITION ( partition_name_list ) */ ignore_leaves ;

ignore_leaves:
	| IGNORE LEAVES ;

set_key_buffer_size:
	/*!50400 SET GLOBAL cache_name.key_buffer_size = _tinyint_unsigned   */ |
	/*!50400 SET GLOBAL cache_name.key_buffer_size = _smallint_unsigned  */ |
	/*!50400 SET GLOBAL cache_name.key_buffer_size = _mediumint_unsigned */ ;

set_key_cache_block_size:
	/*!50400 SET GLOBAL cache_name.key_cache_block_size = key_cache_block_size_enum */ ;

key_cache_block_size_enum:
	512 | 1024 | 2048 | 4096 | 8192 | 16384 ;

cache_name:
	c1 | c2 | c3 | c4;

select_explain:
	EXPLAIN /*!50100 PARTITIONS */ SELECT _field FROM table_name_letter where ;

create_select:
	SELECT `col_int_nokey` % 10 AS `col_int_nokey` , `col_int_key` % 10 AS `col_int_key` FROM table_name_letter where ;

select:
	SELECT `col_int_nokey` % 10 AS `col_int_nokey` , `col_int_key` % 10 AS `col_int_key` FROM dml_table_name    where ;

# WHERE clauses suitable for partition pruning
where:
	|                                      |
	WHERE _field comparison_operator value |
	WHERE _field BETWEEN value AND value   ;

comparison_operator:
        > | < | = | <> | != | >= | <= ;

insert:
        insert_replace INTO dml_table_name ( `col_int_nokey`, `col_int_key` ) VALUES ( value , value ) , ( value , value )                     |
        insert_replace INTO dml_table_name ( `col_int_nokey`, `col_int_key` ) select ORDER BY `col_int_key` , `col_int_nokey` LIMIT limit_rows ;

insert_replace:
        INSERT | REPLACE ;

update:
        UPDATE dml_table_name SET _field = value WHERE _field = value ;

delete:
        DELETE FROM dml_table_name WHERE _field = value ORDER BY `col_int_key` , `col_int_nokey` LIMIT limit_rows ;

dml_table_name:
	table_name_part_ext | table_name_part_ext | table_name_part_ext | table_name_part_ext |
	table_name_part_ext | table_name_part_ext | table_name_part_ext | table_name_part_ext |
	table_name_nopart                                                                     ;

table_name_part_ext:
	{ our $ind= 0; return undef }
	table_name_part PARTITION (part_list last_part_elem) ;

part_list:
        part_list_elem_10 | 
        part_list part_list_elem_10 |
        part_list part_list_elem_10 |
        part_list part_list_elem_10 |
        part_list part_list_elem_10 |
        part_list part_list_elem_10 
	;

part_list_100:
        part_list_elem_10 part_list_elem_10 part_list_elem_10 part_list_elem_10
        part_list_elem_10 part_list_elem_10 part_list_elem_10 part_list_elem_10
        part_list_elem_10 part_list_elem_10
	last_part_elem
        ;

part_list_elem_10:
        part_elem part_elem part_elem part_elem part_elem
        part_elem part_elem part_elem part_elem part_elem ;

part_elem:
        { return "p".$ind++."," } ;

last_part_elem:
        { return "p".$ind++ } ;





table_name_nopart:
	a | b ;

table_name_part:
	c | d | e | f | g | h | i | j | k | l | m | n | o | p | q | r | s | t | u | v | w | x | y | z ;

value:
        _digit ;

_field:
        `col_int_nokey` | `col_int_nokey` ;

create_sel:
        create_part | create_part | create_part | create_nopart | create_nopart ;

create_part:
	CREATE TABLE if_not_exists table_name_part (
		`col_int_nokey` INTEGER,
		`col_int_key` INTEGER NOT NULL,
		KEY (`col_int_key`)
	) ENGINE = engine /*!50100 partition */ create_select ;

create_nopart:
        CREATE TABLE if_not_exists table_name_nopart (
                `col_int_nokey` INTEGER,
                `col_int_key` INTEGER NOT NULL,
                KEY (`col_int_key`)
        ) ENGINE = engine create_select ;

table_name_letter:
	table_name_part   |
	table_name_nopart ;
drop:
	DROP TABLE if_exists table_name_letter ;

alter:
	/*!50400 ALTER TABLE table_name_letter alter_operation */;

alter_operation:
	partition                                                           |
	enable_disable KEYS                                                 |
	ADD PARTITION (PARTITION partition_name VALUES LESS THAN MAXVALUE)  |
	ADD PARTITION (PARTITION p125 VALUES LESS THAN MAXVALUE)             |
	COALESCE PARTITION one_two                                          |
	ANALYZE PARTITION partition_name_list                               |
	CHECK PARTITION partition_name_list                                 |
	REBUILD PARTITION partition_name_list                               |
	REPAIR PARTITION partition_name_list                                |
	REMOVE PARTITIONING                                                 |
	OPTIMIZE PARTITION partition_name_list                              |
	ENGINE = engine                                                     |
	ORDER BY _field                                                     |
	TRUNCATE PARTITION partition_name_list		# can not be used in comparison tests against 5.0
;
#	REORGANIZE PARTITION partition_name_list                            |
#       EXCHANGE PARTITION partition_name WITH TABLE table_name_nopart      |
#	DROP PARTITION partition_name                                       |

one_two:
	1 | 2;

partition_name_list:
	{ our $ind= 0; return undef }
	part_list last_part_elem ;

partition_name:
	{ our $nb_part_list= $prng->int(0,$nb_parts) } ;


enable_disable:
	ENABLE | DISABLE ;

# Give preference to MyISAM because key caching is specific to MyISAM

engine:
	MYISAM |
	INNODB | MEMORY          ;

partition:
	{ our $nb_part_list= $prng->int($nb_parts-5,$nb_parts); return undef }
	partition_by_range ;

subpartition:
	|
	SUBPARTITION BY linear HASH ( _field ) SUBPARTITIONS partition_count ;

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

partition_hash_or_key:
	HASH ( field_name ) PARTITIONS partition_count |
	KEY  ( field_name ) PARTITIONS partition_count ;

partition_by_range:
          range_elements { our $ind= 0; return undef } PARTITION BY RANGE ( _field ) (
          range_list
          PARTITION {"p".$ind++} VALUES LESS THAN MAXVALUE );

range_elements:
          { our @range_list; for (my $i=0; $i<$nb_parts; $i++) { push (@range_list, "PARTITION p$i VALUES LESS THAN (".(($i+1)*3)."),")}; return undef } ;

range_list_elem_10:
        range_elem range_elem range_elem range_elem range_elem
        range_elem range_elem range_elem range_elem range_elem ;

range_list:
	range_list_elem_10 range_list_elem_10 range_list_elem_10 range_list_elem_10 
	range_list_elem_10 range_list_elem_10 range_list_elem_10 range_list_elem_10 
	range_list_elem_10 range_list_elem_10  
        ;

range_elem:
        { $ind<$nb_part_list ? return @range_list[$ind++] : "" } ;

limit_rows:
	1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 ;

partition_count:
	96 | 97 | 98 | 98 | 98 | 99 | 99 | 99 | 99 ;

if_exists:
	IF EXISTS ;   

if_not_exists:
	IF NOT EXISTS ;
