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
# This grammar file will execute server operations in parallel to BACKUP / 
# RESTORE
# 
# Associated files are:
#  mysql-test/gentest/conf/backup/backup_obj.yy
#  mysql-test/gentest/conf/backup/backup_interop.yy
#  mysql-test/gentest/lib/Gentest/Reporter/BackupInterop.pm
#
#####################################################################

query:
      server_operation ;

server_operation:
        rotate_event | event_scheduler | sql_mode | character_set | engine |
        time_zone | key_cache_size | key_buffer_size | binlog_format |
        tx_isolation ;

rotate_event:
        FLUSH LOGS | FLUSH TABLES | FLUSH PRIVILEGES ;

sql_mode:
        SET SQL_MODE=' ' | SET SQL_MODE='TRADITIONAL' | 
        SET SQL_MODE='PIPES_AS_CONCAT' | SET SQL_MODE='ANSI_QUOTES' ;

tx_isolation:
        SET GLOBAL TRANSACTION ISOLATION LEVEL READ UNCOMMITTED | 
        SET GLOBAL TRANSACTION ISOLATION LEVEL READ COMMITTED |
        SET GLOBAL TRANSACTION ISOLATION LEVEL REPEATABLE READ |
        SET GLOBAL TRANSACTION ISOLATION LEVEL SERIALIZABLE ;   

character_set:
        SET CHARACTER_SET_CLIENT=cset | SET CHARACTER_SET_CONNECTION=cset | 
        SET CHARACTER_SET_DATABASE=cset | SET CHARACTER_SET_FILESYSTEM=cset | 
        SET CHARACTER_SET_RESULTS=cset | SET CHARACTER_SET_SERVER=cset | 
        SET COLLATION_CONNECTION=coll | SET COLLATION_DATABASE=coll | 
        SET COLLATION_SERVER=coll ;

cset: LATIN1 | LATIN2 | LATIN5 | LATIN7 | SWE7 | BIG5 | EUCJPMS |
      GB2312 | ARMSCII8 | CP932 | UTF8 ;

coll: LATIN1_SWEDISH_CI | LATIN2_GENERAL_CI | LATIN5_TURKISH_CI | 
      LATIN7_GENERAL_CI | SWE7_SWEDISH_CI | BIG5_CHINESE_CI | 
      EUCJPMS_JAPANESE_CI | GB2312_CHINESE_CI | ARMSCII8_GENERAL_CI | 
      CP932_JAPANESE_CI | UTF8_SPANISH_CI ;

engine: SET STORAGE_ENGINE=storage_engine ;

storage_engine:  Myisam | Innodb ;

time_zone: SET TIME_ZONE=time ;

time: 'UTC' | 'Universal' | 'Europe/Moscow' | 'Japan' ;

key_buffer_size: SET GLOBAL KEY_BUFFER_SIZE=0 |
                 SET GLOBAL KEY_BUFFER_SIZE=400000 ;

key_cache_size: SET GLOBAL QUERY_CACHE_SIZE=1600000 | 
                SET GLOBAL QUERY_CACHE_SIZE=4198490 ;

event_scheduler: SET GLOBAL EVENT_SCHEDULER=ON | 
                 SET GLOBAL EVENT_SCHEDULER=OFF ;

binlog_format: SET GLOBAL BINLOG_FORMAT = ROW | 
               SET GLOBAL BINLOG_FORMAT = STATEMENT |
               SET GLOBAL BINLOG_FORMAT = MIXED ;
            
