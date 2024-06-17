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
##########################################################################
# Initialization of tables with focus on partitioned tables.

query_init:
        {our $nb_parts= 160; our $nb_values= 30; return undef }
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

##########################################################################
# Cache and Load index

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

##########################################################################
# DML statements

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

value:
        _digit ;

update:
        UPDATE dml_table_name SET _field = value WHERE _field = value ;

delete:
        DELETE FROM dml_table_name WHERE _field = value ORDER BY `col_int_key` , `col_int_nokey` LIMIT limit_rows ;
 
limit_rows:
	1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 ;

##########################################################################
# Table name including partition expension for DML statements

dml_table_name:
	table_name_part_ext | table_name_part_ext | table_name_part_ext | table_name_part_ext |
	table_name_part_ext | table_name_part_ext | table_name_part_ext | table_name_part_ext |
	table_name_nopart                                                                     ;

table_name_part_ext:
	{ our $nb_parts_var= $prng->int($nb_parts-9,$nb_parts); return undef } 
	table_name_part PARTITION ( part_elem ) ;

part_elem:
	{ my $part_list= ""; for (my $i=0; $i<$nb_parts_var; $i++) { $part_list .= "p".$i."," }; return $part_list."p".$nb_parts_var } ;

table_name_nopart:
	a | b ;

table_name_part:
	c | d | e | f | g | h | i | j | k | l | m | n | o | p | q | r | s | t | u | v | w | x | y | z ;

##########################################################################
# DDL statements

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

if_exists:
	IF EXISTS ;   

if_not_exists:
	IF NOT EXISTS ;

alter:
	/*!50400 ALTER TABLE table_name_letter alter_operation */;

alter_operation:
	partition                                                           |
	enable_disable KEYS                                                 |
	ADD PARTITION (PARTITION partition_name VALUES LESS THAN MAXVALUE)  |
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

##########################################################################
# Elements used in DDL statements

partition_name_list:
	part_elem ;

partition_name:
	{ our $nb_part_list= $prng->int(0,$nb_parts); return "p".$nb_part_list } ;


enable_disable:
	ENABLE | DISABLE ;

# Give preference to MyISAM because key caching is specific to MyISAM

engine:
	MYISAM |
	INNODB | MEMORY          ;

partition:
	partition_by_hash  |
	partition_by_range |
	partition_by_key  ;

subpartition:
	|
	SUBPARTITION BY linear HASH ( _field ) SUBPARTITIONS sub_partition_count ;

linear:
	| LINEAR ;

sub_partition_count:
	{ return $prng->int(1,3) } ;

##########################################################################
# Rules for hash/key partitions

partition_by_hash:
        PARTITION BY linear HASH ( _field ) PARTITIONS partition_count;

partition_by_key:
        PARTITION BY KEY(`col_int_key`) PARTITIONS partition_count ;

partition_count:
	{ return $prng->int($nb_parts-9,$nb_parts) } ;

##########################################################################
# Rules for range partitions

partition_by_range:
	{ our $nb_parts_var= $prng->int($nb_parts-9,$nb_parts); return undef }
        PARTITION BY RANGE ( _field ) ( range_list ) ;

range_list:
	{ my $range_list= ""; for (my $i=0; $i<$nb_parts_var; $i++) { $range_list .= "PARTITION p$i VALUES LESS THAN (".(($i+1)*3).")," }; return $range_list."PARTITION p".$nb_parts_var." VALUES LESS THAN MAXVALUE" } ;

##########################################################################
# Rules for list partitions

partition_by_list:
		         { our $nb_parts_var= $prng->int($nb_parts-9,$nb_parts); return undef }
        PARTITION BY LIST ( _field ) ( list ) ;

list:
    
        { my $list= ""; for (my $i=0; $i<$nb_parts_var; $i++) { $list .= "PARTITION p$i VALUES IN ("; for (my $j=0; $j<$nb_values; $j++) {$list.= ($i*$nb_values+$j).(($j<$nb_values-1) ? "," : "")}; $list.= ($i<$nb_parts_var-1) ? ")," : ")" }; return $list } ;

##########################################################################
# Common elements used in SQL statements

_field:
        `col_int_nokey` | `col_int_nokey` ;

populate_digits:
	{ @digits = @{$prng->shuffleArray([0..9])} ; return undef } ;

shift_digit:
	{ shift @digits } ;

