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

#####################################################################
#
# Author: Jorgen Loland
# Date: April 2009
#
# Purpose: Implementation of WL#4218: Test that transactions executed
# concurrently with backup are either completely restored or not
# restored at all. No transaction should be partially represented
# after restore.
#
# Associated files: 
#   mysql-test/gentest/conf/backup/invariant.yy
#   mysql-test/gentest/conf/backup/invariant.zz
#   mysql-test/gentest/lib/GenTest/Validator/Invariant.pm
#   mysql-test/gentest/lib/GenTest/Reporter/BackupAndRestoreInvariant.pm
#
#####################################################################
#

# Note 1: THIS TEST SCRIPT ASSUMES THAT THE INNODB OR FALCON STORAGE
# ENGINE IS USED.
#
# Note 2: THIS TEST SCRIPT ASSUMES THAT TRANSACTIONS THAT GET ERROR
# 1048 (INSERT RECORD WITH 'NULL' VALUE INTO A 'NOT NULL' COLUMN) IS
# ABORTED. THIS IS DONE BY Invariant.pm
#
# This script performs zero-sum transactions. There are two identical
# tables that store bank accounts for two different banks. Initially,
# there is a total of 1000 [1] bank account records with $100 each.
#
#
# Conceptual database schema:
#
# .-----------------.     .-----------------.
# | bank1_accounts  |     | bank2_accounts  |
# |-----------------|     |-----------------|
# | *account_number |     | *account_number |
# | balance         |     | balance         |
# `-----------------'     `-----------------'
#
# Actual database schema:
#
# .------------------------------.  .------------------------------.
# | table490_innodb_int_autoinc  |  | table510_innodb_int_autoinc  |
# |------------------------------|  |------------------------------|
# | *pk                          |  | *pk                          |
# | int_not_null                 |  | int_not_null                 |
# `------------------------------'  `------------------------------'
# 
# The table names vary with different storage engines, so they
# should not be used as constants. To select a random table, use rule
# "pick_tbl".
# 
# Each transaction performs multiple insert/update/delete operations.
# Any transaction that commits has to follow this rule:
#
#   Any money withdrawn from an account has to be deposited 
#   in another account, possibly in the other bank.
#
# Transactions that abort are allowed to not follow this rule.
#
# invariant.[yy|zz] are accompanied with validator Invariant.pm [2]
# and reporter BackupAndRestoreInvariant.pm [3]. When executed
# together, these modules check that BACKUP/RESTORE is transactionally
# consistent: Any transaction executing concurrently with backup is
# either completely included in the database after restore, or is not
# included at all.
#
# [1] The two tables have 490 and 510 records initially (a small
# difference only so that RQG will generate two different table
# names). If the initial number of records is changed in invariant.zz,
# upd_range should likely be changed as well.
#
# [2] Checks that the total account balance remains the same and rolls
# back transactions that need to be aborted
#
# [3] Performs BACKUP and RESTORE, and checks that the total account
# balance is correct after restore.

query_init:
  SET AUTOCOMMIT=OFF ; 

query:
  commit_trans |
  commit_trans |
  commit_trans |
  commit_trans |
  abort_trans ;

commit_trans:
  START TRANSACTION ; body ; COMMIT ;

abort_trans:
  START TRANSACTION ; body ; ROLLBACK ;

body:
   update_two |                                  
   update_two_scan |
   update_four | 
   update_sixteen |
   update_sixteen_sleep |
   delete_insert_two |
   insert_two |       
   delete_update |    
   # delete_insert_two commits, but before doing any write operation. 
   # Safe to run updates *after* delete_i_t, but not reverse order
   delete_insert_two ; update_four ; update_four ;

# The Robin Hood script, pt1
# Delete one of the records with highest account balance and deposit the
# same amount to another record with a low account balance. 
# WARNING: A "COMMIT ; START TRANSACTION" is part of this script
# See documentation for delete_insert_two
# NOTE: The final delete must be performed on the same record (and
# therefore same table) as @val is selected from. Hence the use of
# $tbl_cnst for this part of the script.
delete_update:
  prepare_delete ; UPDATE $tbl_cnst SET `col_int_not_null`=`col_int_not_null`+1 WHERE pk=@delpk ; SELECT @val:=`col_int_not_null` FROM $tbl_cnst WHERE pk=@delpk ; SET @val=@val-1 ; update_low_randtbl ; DELETE from $tbl_cnst WHERE pk=@delpk ; check_val_not_null ;

# Insert a record with int_not_null=@val and delete it. This will make
# the transaction abort if @val=null
check_val_not_null:
  insert_one ; SELECT @ins_id:=LAST_INSERT_ID() ; DELETE from $tbl WHERE pk=@ins_id ;

# The Robin Hood script, pt2
# Delete one of the records with highest account balance and deposit the
# same total amount to two new records. 
# WARNING: A "COMMIT ; START TRANSACTION" is part of this script
# NOTE: Since we get @delpk by selecting from the table, we must start
# a new transaction (done in prepare_delete). Otherwise, due to
# multiversion concurrency control, "select @val ..." will get an old
# value if the @delpk record is updated or deleted in a concurrent
# thread. This results in missing update anomaly.
# NOTE: @val must be set to NULL initially, otherwise it retains old
# values if the @delpk record is deleted by a concurrent thread (since
# the result set is empty in this case). By setting @val to NULL, the
# following inserts will be rejected by the server and thus have no
# effect.
# NOTE: The first update (+1) is performed to set a lock. Without this
# update, the following select may get an old value after a concurrent
# thread has deleted the record.
# NOTE: The final delete must be performed on the same record (and
# therefore same table) as @val is selected from. Hence the use of
# $tbl_cnst for this part of the script.
delete_insert_two:
  prepare_delete ; UPDATE $tbl_cnst SET `col_int_not_null`=`col_int_not_null`+1 WHERE pk=@delpk ; SELECT @val:=`col_int_not_null`-1 FROM $tbl_cnst WHERE pk=@delpk ; insert_two_valsum ; DELETE from $tbl_cnst WHERE pk=@delpk ; 

# Perform neccessary steps before a record is deleted. 
# RETURN $tbl_cnst
# RETURN @delpk
prepare_delete:
  SET @val=NULL ; pick_tbl_const ; set_delpk ; COMMIT ; START TRANSACTION ;

# Add two records with a total account balance of @val
# NOTE: SET @val=@val-@val+@myprime must not be changed to
# @val=@myprime because @val must remain NULL if it was NULL before
# the assignment. This prevents insert of a record with account
# balance @myprime in the case where @delpk has been deleted by a
# concurrent thread.
insert_two_valsum:
  SET @myprime=prime ; SET @val=@val-@myprime ; insert_one_randtbl ; SET @val=@val-@val+@myprime ; insert_one_randtbl ;

# Add two records, one with positive and one with negative account
# balance. The balance is chosen randomly.
insert_two:
  SET @val=prime ; insert_one_randtbl ; SET @val=-@val ; insert_one_randtbl ; 

# Add one record with account balance defined by @val into a random table
insert_one_randtbl:
  pick_tbl ; insert_one ;

# Add one record with account balance defined by @val into $tbl
insert_one:
  INSERT INTO $tbl(`col_int_not_null`) VALUES (@val) ;

# Update two records by withdrawing from one account and depositing
# into the other. The amount moved between accounts is chosen
# randomly.
update_two:
  SET @val=prime ; update_low_randtbl ; SET @val=-@val ; update_high_randtbl ;

# Update four records by depositing into three accounts and
# withdrawing the total from one account. This way, updates are not
# always symmetric (increase and decrease the same amount). Each
# updated account is chosen on random from a random bank.
update_four:
  SET @val=prime ; SET @total=@val ; update_low_randtbl ; SET @val=prime ; SET @total=@total+@val ; update_low_randtbl ; SET @val=prime ; SET @total=@total+@val ; update_low_randtbl ; SET @val=-@total ; update_high_randtbl ;

update_sixteen: 
  update_four ; update_four ; update_four ; update_four ; 

# Significantly increase execution time of 16 updates by sleeping for
# a total of 3 seconds.
update_sixteen_sleep: 
  update_four ; SELECT SLEEP(1); update_four ; SELECT SLEEP(1); update_four ; SELECT SLEEP(1); update_four ; SELECT SLEEP(1); 

# Update one of the records with lowest account balance by adding @val to it
# Requirement for use: @val must have an integer value
update_low_randtbl:
  pick_tbl ; set_updpk_low ; UPDATE $tbl SET `col_int_not_null`=`col_int_not_null`+(@val) WHERE pk=@updpk ;
  
# Update one of the records with highest account balance by adding @val to it
# Requirement for use: @val must have an integer value
update_high_randtbl:
  pick_tbl ; set_updpk_high ; UPDATE $tbl SET `col_int_not_null`=`col_int_not_null`+(@val) WHERE pk=@updpk ;

update_two_scan:
  SET @val=prime ; update_low_randtbl_scan ; SET @val=-@val ; update_high_randtbl_scan ;

# Update one for the records with lowest account balance by adding
# @val to it. The update has to perform a table scan to find the
# record to update.
update_low_randtbl_scan:
  pick_tbl ; UPDATE $tbl SET `col_int_not_null`=`col_int_not_null`+@val WHERE pk IN (SELECT pk FROM (SELECT pk FROM $tbl WHERE `col_int_not_null` IN (SELECT MIN(`col_int_not_null`) FROM $tbl WHERE pk<=upd_range)) AS tmp1) ORDER BY rand() LIMIT 1;

# Update one for the records with highest account balance by adding
# @val to it. The update has to perform a table scan to find the
# record to update.
update_high_randtbl_scan:
  pick_tbl ; UPDATE $tbl SET `col_int_not_null`=`col_int_not_null`+@val WHERE pk IN (SELECT pk FROM (SELECT pk FROM $tbl WHERE `col_int_not_null` IN (SELECT MAX(`col_int_not_null`) FROM $tbl WHERE pk<=upd_range)) AS tmp1) ORDER BY rand() LIMIT 1;
# Before "where pk<=" was moved inside innermost select, the subselect would sometimes return an empty set
#  pick_tbl ; UPDATE $tbl SET `col_int_not_null`=`col_int_not_null`+@val WHERE pk IN (SELECT pk FROM (SELECT pk FROM $tbl WHERE pk<=upd_range AND `col_int_not_null` IN (SELECT MAX(`col_int_not_null`) FROM $tbl)) AS tmp1) ORDER BY rand() LIMIT 1;

# Get one primary key of an existing record in the update range of the records
# RETURN @updpk
set_updpk_low:
  SELECT @updpk:=`pk` FROM (pk_upd_low) AS tbl ;

set_updpk_high:
  SELECT @updpk:=`pk` FROM (pk_upd_high) AS tbl ;

pk_upd_low:
  SELECT `pk` FROM $tbl WHERE `pk` <= upd_range ORDER BY `col_int_not_null` ASC LIMIT prime ;

pk_upd_high:
  SELECT `pk` FROM $tbl WHERE `pk` <= upd_range ORDER BY `col_int_not_null` DESC LIMIT prime ;

# Get one primary key of an existing record in the insert/delete range of the records
# RETURN @delpk
set_delpk:
  SELECT @delpk:=`pk` FROM $tbl_cnst WHERE `pk` > upd_range ORDER BY `col_int_not_null` DESC LIMIT prime ;

# Pick a random table to operate on
# RETURN $tbl
pick_tbl:
  { $tbl = $prng->arrayElement($executors->[0]->tables()) ; return undef ; } ;

# For transactions that need to operate on the same table more than
# once. By calling this rule one time only for a transaction,
# $tbl_cnst will remain unchanged for later use withing that transaction.
# RETURN $tbl_cnst
pick_tbl_const:
  { $tbl_cnst = $prng->arrayElement($executors->[0]->tables()) ; return undef ; } ;

# Update operations are performed on records with a pk lower than this
# number, while delete operations are performed on records with a
# higher pk.
upd_range:
  250 ;

prime:
  1 | 2 | 3 | 5 | 7 | 11 | 13 | 17 | 23 | 29 | 31 ;
