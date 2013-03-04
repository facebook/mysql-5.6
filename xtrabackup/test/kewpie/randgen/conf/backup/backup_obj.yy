# Copyright (C) 2008 Sun Microsystems, Inc. All rights reserved.
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

#####################################################################
#
# Author: Hema Sridharan
# Date: May 2009
#
# Purpose: Implementation of WL#4732: The test is intended to verify
# the interoperability of MySQL BACKUP / RESTORE operations with other
# server operations.
# This grammar file creates tables and objects in the database db1.
#
#  Associated files are:
#  mysql-test/gentest/conf/backup/backup_obj.yy
#  mysql-test/gentest/conf/backup/backup_interop.yy
#  mysql-test/gentest/lib/Gentest/Reporter/BackupInterop.pm
#
#####################################################################
query:
      create_table | create_view |
      create_procedure | create_function | 
      create_trigger | create_event ;
#     sql_mode | character_set;

# NOTE: sql_mode and character set is disabled because of BUG#46419
# BUG#46419: Memory corruption when creating tables with different character 
# sets in RQG

create_table:
       CREATE TABLE IF NOT EXISTS table_name (table_cols) ENGINE = storage_engine ;

storage_engine:  Myisam | Innodb ;

table_cols: one_col | two_col | three_col ;

one_col: column_definition ;

two_col: column_definition,
         column_definition ;

three_col: column_definition,
           column_definition,
           column_definition ;

column_definition: new_column data_type column_option ;

new_column: c1 | c2 | c3 | c4 | c5 | c6 | c7 | c8 | c9 | c10 ;

data_type: INT | CHAR(255) | TINYINT | VARCHAR(255) | SMALLINT | BIGINT | TIME
| YEAR | TIMESTAMP | DATETIME | DATE | TEXT | BLOB ;

column_option: NULL | NOT NULL ;

table_name:
            t1 | t2 | t3 | t4 | t5 | t6 | t7 | t8 | t9 | t10 ;

create_view:
        CREATE VIEW view_name AS SELECT * FROM table_name ;

view_name:
        v1 | v2 | v3 | v4 | v5 | v6 | v7 | v8 | v9 | v10 ;

create_procedure:
        CREATE PROCEDURE db1.procedure_name () SET @pro=10 ;

procedure_name:
               p1 | p2 | p3 | p4 | p5 | p6 | p7 | p8 | p9 | p10 ;

call_procedure:
        CALL db1.procedure_name ;

create_function:
       CREATE FUNCTION function_name () RETURNS INT RETURN (SELECT COUNT(*) FROM table_or_view) ;

table_or_view:
        table_name | view_name;

function_name:
           f1 | f2 | f3 | f4 | f5 | f6 | f7 | f8 | f9 | f10 ;

create_trigger:
        CREATE TRIGGER trigger_name trigger_time trigger_event ON table_name FOR EACH ROW SET @trg=100 ;

trigger_name:
        letter ;

trigger_time:
        BEFORE | AFTER ;

trigger_event:
        INSERT | UPDATE ;

create_event:
         CREATE EVENT event_name ON SCHEDULE EVERY digit SECOND ON COMPLETION PRESERVE DO SET @ev=100 ;

event_name:
          letter ;

sql_mode:
        SET SQL_MODE=' ' | SET SQL_MODE='TRADITIONAL' | 
        SET SQL_MODE='PIPES_AS_CONCAT' | SET SQL_MODE='ANSI_QUOTES' ;

character_set:
        SET CHARACTER_SET_CLIENT=cset | SET CHARACTER_SET_CONNECTION=cset | 
        SET CHARACTER_SET_DATABASE=cset | SET CHARACTER_SET_FILESYSTEM=cset | 
        SET CHARACTER_SET_RESULTS=cset | SET CHARACTER_SET_SERVER=cset | 
        SET COLLATION_CONNECTION=coll | SET COLLATION_DATABASE=coll | 
        SET COLLATION_SERVER=coll ;

cset: LATIN1 | LATIN2 | LATIN5 | LATIN7 | SWE7 | BIG5 | 
      EUCJPMS | GB2312 | ARMSCII8 | CP932 | UTF8 ;

coll: LATIN1_SWEDISH_CI | LATIN2_GENERAL_CI | LATIN5_TURKISH_CI | 
      LATIN7_GENERAL_CI | SWE7_SWEDISH_CI | BIG5_CHINESE_CI | 
      EUCJPMS_JAPANESE_CI | GB2312_CHINESE_CI | ARMSCII8_GENERAL_CI | 
      CP932_JAPANESE_CI | UTF8_SPANISH_CI ;

