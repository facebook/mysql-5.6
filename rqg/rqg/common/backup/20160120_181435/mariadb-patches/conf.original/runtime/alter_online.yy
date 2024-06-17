# Copyright (c) 2012 Oracle and/or its affiliates. All rights reserved.
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

start_delay:
   # Avoid that worker threads cause a server crash before reporters are started.
   # This leads often to STATUS_ENVIRONMENT_ERROR though a crash happened.
   { sleep 5; return undef };

query_init:
   start_delay ; SET AUTOCOMMIT = 0; create_table ; SET @fill_amount = (@@innodb_page_size / 2 ) + 1;

create_table:
   CREATE TABLE IF NOT EXISTS t1 (col1 INT, col2 INT, col3 INT, col4 TEXT) ENGINE = InnoDB;

query:
   ddl |
   dml ;

dml:
   # Ensure that the table does not grow endless.                                                                       |
   delete ; COMMIT                                                                                                      |
   # Make likely: Get duplicate key based on the two row INSERT only.                                                   |
   enforce_duplicate1 ;                                                                                 commit_rollback |
   # Make likely: Get duplicate key based on two row UPDATE only.                                                       |
   enforce_duplicate2 ;                                                                                 commit_rollback |
   # Make likely: Get duplicate key based on the row INSERT and the already committed data.                             |
   insert_part ( my_digit , $my_digit, $my_digit, REPEAT(CAST($my_digit AS CHAR(1)),@fill_amount));     commit_rollback |
   insert_part ( my_digit , $my_digit - 1, $my_digit, REPEAT(CAST($my_digit AS CHAR(1)),@fill_amount)); commit_rollback |
   insert_part ( my_digit , $my_digit, $my_digit - 1, REPEAT(CAST($my_digit AS CHAR(1)),@fill_amount)); commit_rollback |
   insert_part ( my_digit , $my_digit, $my_digit, REPEAT(CAST($my_digit - 1 AS CHAR(1)),@fill_amount)); commit_rollback ;

enforce_duplicate1:
   delete ; insert_part /* my_digit */ some_record , some_record ;

enforce_duplicate2:
   UPDATE t1 SET column_name_int = my_digit LIMIT 2 ;

insert_part:
   INSERT INTO t1 (col1,col2,col3,col4) VALUES ;

some_record:
   ($my_digit,$my_digit,$my_digit,REPEAT(CAST($my_digit AS CHAR(1)),@fill_amount)) ;

delete:
   DELETE FROM t1 WHERE column_name_int = my_digit OR $column_name_int IS NULL                              |
   DELETE FROM t1 WHERE MATCH(col4) AGAINST (TRIM(' my_digit ') IN BOOLEAN MODE) OR column_name_int IS NULL ;

my_digit:
   { $my_digit= 1 }      |
   { $my_digit= 2 }      |
   { $my_digit= 3 }      |
   { $my_digit= 4 }      |
   { $my_digit= 'NULL' } ;

commit_rollback:
   COMMIT   |
   ROLLBACK ;

ddl:
   ALTER TABLE t1 add_accelerator                     |
   ALTER TABLE t1 add_accelerator                     |
   ALTER TABLE t1 add_accelerator                     |
   ALTER TABLE t1 add_accelerator                     |
   ALTER TABLE t1 drop_accelerator                    |
   ALTER TABLE t1 drop_accelerator                    |
   ALTER TABLE t1 drop_accelerator                    |
   ALTER TABLE t1 drop_accelerator                    |
   ALTER TABLE t1 add_accelerator  , add_accelerator  |
   ALTER TABLE t1 drop_accelerator , drop_accelerator |
   ALTER TABLE t1 drop_accelerator , add_accelerator  |
   check_table                                        |
   replace_column                                     ;

add_accelerator:
   ADD  UNIQUE   KEY  uidx ( column_name_list ) |
   ADD           KEY   idx ( column_name_list ) |
   ADD  PRIMARY  KEY       ( column_name_list ) |
   ADD  FULLTEXT KEY ftidx ( col4             ) ;

drop_accelerator:
   DROP         KEY  uidx |
   DROP         KEY   idx |
   DROP         KEY ftidx |
   DROP PRIMARY KEY       ;

check_table:
   CHECK TABLE t1 ;

column_name_int:
   { $column_name_int= 'col1' } |
   { $column_name_int= 'col2' } |
   { $column_name_int= 'col3' } ;

column_name_list:
   column_name_int           |
   column_name_int           |
   column_name_int           |
   column_name_int           |
   col4(10)                  |
   col4(10), column_name_int ;

replace_column:
   ALTER TABLE t1 ADD COLUMN extra INT; UPDATE t1 SET extra = column_name_int ; ALTER TABLE t1 DROP COLUMN $column_name_int ; ALTER TABLE t1 CHANGE COLUMN extra $column_name_int INT ;
