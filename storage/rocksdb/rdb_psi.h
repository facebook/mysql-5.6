/* Copyright (c) 2017, Percona and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */
#pragma once

#ifndef _rdb_psi_h_
#define _rdb_psi_h_

/* MySQL header files */

#include <mysql/psi/mysql_cond.h>
#include <mysql/psi/mysql_file.h>
#include <mysql/psi/mysql_mutex.h>
#include <mysql/psi/mysql_rwlock.h>
#include <mysql/psi/mysql_stage.h>
#include <mysql/psi/mysql_thread.h>

/* MyRocks header files */
#include "./rdb_utils.h"

namespace myrocks {

/*
  The following is needed as an argument for mysql_stage_register,
  irrespectively of whether we're compiling with P_S or not.
*/
extern my_core::PSI_stage_info stage_waiting_on_row_lock;

#ifdef HAVE_PSI_INTERFACE
extern my_core::PSI_thread_key rdb_background_psi_thread_key,
    rdb_drop_idx_psi_thread_key, rdb_is_psi_thread_key, rdb_mc_psi_thread_key;

extern my_core::PSI_mutex_key rdb_psi_open_tbls_mutex_key,
    rdb_signal_bg_psi_mutex_key, rdb_signal_drop_idx_psi_mutex_key,
    rdb_signal_is_psi_mutex_key, rdb_signal_mc_psi_mutex_key,
    rdb_collation_data_mutex_key, rdb_mem_cmp_space_mutex_key,
    key_mutex_tx_list, rdb_sysvars_psi_mutex_key, rdb_cfm_mutex_key,
    rdb_sst_commit_key, rdb_block_cache_resize_mutex_key,
    rdb_bottom_pri_background_compactions_resize_mutex_key,
    clone_donor_file_metadata_mutex_key, clone_main_task_remaining_mutex_key,
    clone_error_mutex_key;

extern my_core::PSI_rwlock_key key_rwlock_collation_exception_list,
    key_rwlock_read_free_rpl_tables, key_rwlock_clone_task_id_set,
    key_rwlock_clone_active_clones, key_rwlock_clone_client_files;

extern my_core::PSI_cond_key rdb_signal_bg_psi_cond_key,
    rdb_signal_drop_idx_psi_cond_key, rdb_signal_is_psi_cond_key,
    rdb_signal_mc_psi_cond_key, rdb_signal_clone_main_task_remaining_key,
    rdb_signal_clone_reconnection_key;

extern my_core::PSI_file_key rdb_clone_donor_file_key,
    rdb_clone_client_file_key;

#endif  // HAVE_PSI_INTERFACE

void init_rocksdb_psi_keys();

}  // namespace myrocks

#endif  // _rdb_psi_h_
