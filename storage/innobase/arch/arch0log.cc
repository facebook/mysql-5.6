/*****************************************************************************

Copyright (c) 2017, 2021, Oracle and/or its affiliates.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License, version 2.0, as published by the
Free Software Foundation.

This program is also distributed with certain software (including but not
limited to OpenSSL) that is licensed under separate terms, as designated in a
particular file or component or in included license documentation. The authors
of MySQL hereby grant you an additional permission to link the program and
your derivative works with the separately licensed software that they have
included with MySQL.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License, version 2.0,
for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA

*****************************************************************************/

/** @file arch/arch0log.cc
 Innodb implementation for log archive

 *******************************************************/

#include "arch0log.h"
#include "clone0clone.h"
#include "srv0start.h"

/** Chunk size for archiving redo log */
const uint ARCH_LOG_CHUNK_SIZE = 1024 * 1024;

/** Get redo file, header and trailer size
@param[out]	file_sz		redo file size
@param[out]	header_sz	redo header size
@param[out]	trailer_sz	redo trailer size */
void Log_Arch_Client_Ctx::get_header_size(ib_uint64_t &file_sz, uint &header_sz,
                                          uint &trailer_sz) {
  file_sz = srv_log_file_size;
  header_sz = LOG_FILE_HDR_SIZE;
  trailer_sz = OS_FILE_LOG_BLOCK_SIZE;
}

/** Start redo log archiving
@param[out]	header	redo header. Caller must allocate buffer.
@param[in]	len	buffer length
@return error code */
int Log_Arch_Client_Ctx::start(byte *header, uint len) {
  ut_ad(len >= LOG_FILE_HDR_SIZE);

  auto err = arch_log_sys->start(m_group, m_begin_lsn, header, false);

  if (err != 0) {
    return (err);
  }

  m_state = ARCH_CLIENT_STATE_STARTED;

  ib::info(ER_IB_MSG_15) << "Clone Start LOG ARCH : start LSN : "
                         << m_begin_lsn;

  return (0);
}

/** Stop redo log archiving. Exact trailer length is returned as out
parameter which could be less than the redo block size.
@param[out]	trailer	redo trailer. Caller must allocate buffer.
@param[in,out]	len	trailer length
@param[out]	offset	trailer block offset
@return error code */
int Log_Arch_Client_Ctx::stop(byte *trailer, uint32_t &len, uint64_t &offset) {
  lsn_t start_lsn;
  lsn_t stop_lsn;

  ut_ad(m_state == ARCH_CLIENT_STATE_STARTED);
  ut_ad(trailer == nullptr || len >= OS_FILE_LOG_BLOCK_SIZE);

  auto err = arch_log_sys->stop(m_group, m_end_lsn, trailer, len);

  start_lsn = m_group->get_begin_lsn();

  start_lsn = ut_uint64_align_down(start_lsn, OS_FILE_LOG_BLOCK_SIZE);
  stop_lsn = ut_uint64_align_down(m_end_lsn, OS_FILE_LOG_BLOCK_SIZE);

  lsn_t file_capacity = m_group->get_file_size();

  file_capacity -= LOG_FILE_HDR_SIZE;

  offset = (stop_lsn - start_lsn) % file_capacity;

  offset += LOG_FILE_HDR_SIZE;

  m_state = ARCH_CLIENT_STATE_STOPPED;

  ib::info(ER_IB_MSG_16) << "Clone Stop  LOG ARCH : end LSN : " << m_end_lsn;

  return (err);
}

/** Get archived data file details
@param[in]	cbk_func	callback called for each file
@param[in]	ctx		callback function context
@return error code */
int Log_Arch_Client_Ctx::get_files(Log_Arch_Cbk *cbk_func, void *ctx) {
  ut_ad(m_state == ARCH_CLIENT_STATE_STOPPED);

  int err = 0;

  auto size = m_group->get_file_size();

  /* Check if the archived redo log is less than one block size. In this
  case we send the data in trailer buffer. */
  auto low_begin = ut_uint64_align_down(m_begin_lsn, OS_FILE_LOG_BLOCK_SIZE);

  auto low_end = ut_uint64_align_down(m_end_lsn, OS_FILE_LOG_BLOCK_SIZE);

  if (low_begin == low_end) {
    err = cbk_func(nullptr, size, 0, ctx);
    return (err);
  }

  /* Get the start lsn of the group */
  auto start_lsn = m_group->get_begin_lsn();
  start_lsn = ut_uint64_align_down(start_lsn, OS_FILE_LOG_BLOCK_SIZE);

  ut_ad(m_begin_lsn >= start_lsn);

  /* Calculate first file index and offset for this client. */
  lsn_t lsn_diff = m_begin_lsn - start_lsn;
  uint64_t capacity = size - LOG_FILE_HDR_SIZE;

  auto idx = static_cast<uint>(lsn_diff / capacity);
  uint64_t offset = lsn_diff % capacity;

  /* Set start lsn to the beginning of file. */
  start_lsn = m_begin_lsn - offset;

  offset += LOG_FILE_HDR_SIZE;
  offset = ut_uint64_align_down(offset, OS_FILE_LOG_BLOCK_SIZE);

  /* Callback with all archive file names that holds the range of log
  data for this client. */
  while (start_lsn < m_end_lsn) {
    char name[MAX_ARCH_LOG_FILE_NAME_LEN];
    m_group->get_file_name(idx, name, MAX_ARCH_LOG_FILE_NAME_LEN);

    idx++;
    start_lsn += capacity;

    /* For last file adjust the size based on end lsn. */
    if (start_lsn >= m_end_lsn) {
      lsn_diff =
          ut_uint64_align_up(start_lsn - m_end_lsn, OS_FILE_LOG_BLOCK_SIZE);
      size -= lsn_diff;
    }

    err = cbk_func(name, size, offset, ctx);

    if (err != 0) {
      break;
    }

    /* offset could be non-zero only for first file. */
    offset = 0;
  }

  return (err);
}

/** Release archived data so that system can purge it */
void Log_Arch_Client_Ctx::release() {
  if (m_state == ARCH_CLIENT_STATE_INIT) {
    return;
  }

  if (m_state == ARCH_CLIENT_STATE_STARTED) {
    uint64_t dummy_offset;
    uint32_t dummy_len = 0;

    /* This is for cleanup in error cases. */
    stop(nullptr, dummy_len, dummy_offset);
  }

  ut_ad(m_state == ARCH_CLIENT_STATE_STOPPED);

  arch_log_sys->release(m_group, false);

  m_group = nullptr;

  m_begin_lsn = LSN_MAX;
  m_end_lsn = LSN_MAX;

  m_state = ARCH_CLIENT_STATE_INIT;
}

void Arch_Log_Sys::update_header(const Arch_Group *group, byte *header,
                                 lsn_t checkpoint_lsn) {
  lsn_t start_lsn;
  lsn_t lsn_offset;
  ib_uint64_t file_size;

  start_lsn = group->get_begin_lsn();
  file_size = group->get_file_size();

  start_lsn = ut_uint64_align_down(start_lsn, OS_FILE_LOG_BLOCK_SIZE);

  /* Copy Header information. */
  log_files_header_fill(header, start_lsn, LOG_HEADER_CREATOR_CLONE, false,
                        false);

  ut_ad(checkpoint_lsn >= start_lsn);

  lsn_offset = checkpoint_lsn - start_lsn;
  file_size -= LOG_FILE_HDR_SIZE;

  lsn_offset = lsn_offset % file_size;
  lsn_offset += LOG_FILE_HDR_SIZE;

  /* Update checkpoint lsn and offset. */
  byte *src = header + LOG_CHECKPOINT_1;

  mach_write_to_8(src + LOG_CHECKPOINT_LSN, checkpoint_lsn);
  mach_write_to_8(src + LOG_CHECKPOINT_OFFSET, lsn_offset);
  log_block_set_checksum(src, log_block_calc_checksum_crc32(src));

  /* Copy to second checkpoint location */
  byte *dest = header + LOG_CHECKPOINT_2;

  memcpy(dest, src, OS_FILE_LOG_BLOCK_SIZE);

  /* Fill encryption information. */
  auto redo_space = fil_space_get(dict_sys_t::s_log_space_first_id);
  if (redo_space->encryption_type == Encryption::NONE) {
    return;
  }
  byte *key = redo_space->encryption_key;
  byte *iv = redo_space->encryption_iv;
  dest = header + LOG_ENCRYPTION;

  log_file_header_fill_encryption(dest, key, iv, false, false);
}

/** Start redo log archiving.
If archiving is already in progress, the client
is attached to current group.
@param[out]	group		log archive group
@param[out]	start_lsn	start lsn for client
@param[out]	header		redo log header
@param[in]	is_durable	if client needs durable archiving
@param[in]	stop_lsn	archiving target stop LSN
@return error code */
int Arch_Log_Sys::start(Arch_Group *&group, lsn_t &start_lsn, byte *header,
                        bool is_durable) {
  bool create_new_group = false;

  memset(header, 0, LOG_FILE_HDR_SIZE);

  log_request_checkpoint(*log_sys, true);

  ut_ad(get_stop_lsn_master_thread() == LSN_MAX);

  arch_mutex_enter();

  if (m_state == ARCH_STATE_READ_ONLY) {
    arch_mutex_exit();
    return 0;
  }

  /* Wait for idle state, if preparing to idle. */
  if (!wait_idle()) {
    int err = 0;

    if (srv_shutdown_state.load() >= SRV_SHUTDOWN_CLEANUP) {
      err = ER_QUERY_INTERRUPTED;
      my_error(err, MYF(0));
    } else {
      err = ER_INTERNAL_ERROR;
      my_error(err, MYF(0), "Log Archiver wait too long");
    }

    arch_mutex_exit();
    return (err);
  }

  ut_ad(m_state != ARCH_STATE_PREPARE_IDLE);

  if (m_state == ARCH_STATE_ABORT) {
    arch_mutex_exit();
    my_error(ER_QUERY_INTERRUPTED, MYF(0));
    return (ER_QUERY_INTERRUPTED);
  }

  /* Start archiver task, if needed. */
  if (m_state == ARCH_STATE_INIT) {
    auto err = start_log_archiver_background();

    if (err != 0) {
      arch_mutex_exit();

      ib::error(ER_IB_MSG_17) << "Could not start Archiver"
                              << " background task";

      return (err);
    }
  }

  /* Start archiving from checkpoint LSN. */
  log_writer_mutex_enter(*log_sys);

  start_lsn = log_sys->last_checkpoint_lsn.load();

  auto lsn_offset = log_files_real_offset_for_lsn(*log_sys, start_lsn);

  auto start_index = static_cast<uint>(lsn_offset / log_sys->file_size);
  uint64_t start_offset = lsn_offset % log_sys->file_size;

  /* Copy to first checkpoint location */
  auto src = static_cast<void *>(log_sys->checkpoint_buf);
  auto dest = static_cast<void *>(header + LOG_CHECKPOINT_1);

  memcpy(dest, src, OS_FILE_LOG_BLOCK_SIZE);

  /* Need to create a new group if archiving is not in progress. */
  if (m_state == ARCH_STATE_IDLE || m_state == ARCH_STATE_INIT) {
    m_archived_lsn.store(
        ut_uint64_align_down(start_lsn, OS_FILE_LOG_BLOCK_SIZE));
    create_new_group = true;
  }

  /* Set archiver state to active. */
  if (m_state != ARCH_STATE_ACTIVE) {
    m_state = ARCH_STATE_ACTIVE;
    os_event_set(log_archiver_thread_event);
  }
  log_writer_mutex_exit(*log_sys);

  /* Create a new group. */
  if (create_new_group) {
    m_current_group =
        ut::new_withkey<Arch_Group>(ut::make_psi_memory_key(mem_key_archive),
                                    start_lsn, LOG_FILE_HDR_SIZE, &m_mutex);

    if (m_current_group == nullptr) {
      arch_mutex_exit();

      my_error(ER_OUTOFMEMORY, MYF(0), sizeof(Arch_Group));
      return (ER_OUTOFMEMORY);
    }

    /* Currently use log file size for archived files. */
    auto db_err = m_current_group->init_file_ctx(
        ARCH_DIR, ARCH_LOG_DIR, ARCH_LOG_FILE, 0,
        static_cast<ib_uint64_t>(srv_log_file_size));

    if (db_err != DB_SUCCESS) {
      arch_mutex_exit();

      my_error(ER_OUTOFMEMORY, MYF(0), sizeof(Arch_File_Ctx));

      return (ER_OUTOFMEMORY);
    }

    start_offset = ut_uint64_align_down(start_offset, OS_FILE_LOG_BLOCK_SIZE);

    m_start_log_index = start_index;
    m_start_log_offset = start_offset;

    m_chunk_size = ARCH_LOG_CHUNK_SIZE;

    m_group_list.push_back(m_current_group);
  }

  /* Attach to the current group. */
  m_current_group->attach(is_durable);

  group = m_current_group;

  arch_mutex_exit();

  /* Update header with checkpoint LSN. We need to pass the saved
  group pointer as arch mutex is released and m_current_group should
  no longer be accessed. The group cannot be freed as we have already
  attached to it. */
  update_header(group, header, start_lsn);

  return (0);
}

#ifdef UNIV_DEBUG
void Arch_Group::adjust_end_lsn(lsn_t &stop_lsn, uint32_t &blk_len) {
  stop_lsn = ut_uint64_align_down(get_begin_lsn(), OS_FILE_LOG_BLOCK_SIZE);

  stop_lsn += get_file_size() - LOG_FILE_HDR_SIZE;
  blk_len = 0;

  /* Increase Stop LSN 64 bytes ahead of file end not exceeding
  redo block size. */
  DBUG_EXECUTE_IF("clone_arch_log_extra_bytes",
                  blk_len = OS_FILE_LOG_BLOCK_SIZE;
                  stop_lsn += 64;);
}

void Arch_Group::adjust_copy_length(lsn_t arch_lsn, uint32_t &copy_len) {
  lsn_t end_lsn = LSN_MAX;
  uint32_t blk_len = 0;
  adjust_end_lsn(end_lsn, blk_len);

  if (end_lsn <= arch_lsn) {
    copy_len = 0;
    return;
  }

  /* Adjust if copying beyond end LSN. */
  auto len_left = end_lsn - arch_lsn;
  len_left = ut_uint64_align_down(len_left, OS_FILE_LOG_BLOCK_SIZE);

  if (len_left < copy_len) {
    copy_len = static_cast<uint32_t>(len_left);
  }
}

#endif /* UNIV_DEBUG */

/** Stop redo log archiving.
If other clients are there, the client is detached from
the current group.
@param[out]	group		log archive group
@param[out]	stop_lsn	stop lsn for client
@param[out]	log_blk		redo log trailer block
@param[in,out]	blk_len		length in bytes
@return error code */
int Arch_Log_Sys::stop(Arch_Group *group, lsn_t &stop_lsn, byte *log_blk,
                       uint32_t &blk_len) {
  int err = 0;
  blk_len = 0;

  auto se_sync_stop_lsn = get_stop_lsn_master_thread();
  ut_ad(se_sync_stop_lsn != LSN_MAX || log_blk == nullptr);

  stop_lsn = std::min(m_archived_lsn.load(), se_sync_stop_lsn);
  ut_ad(stop_lsn <= se_sync_stop_lsn);

  if (log_blk != nullptr) {
    DBUG_EXECUTE_IF("clone_arch_log_stop_file_end", {
      // This desyncs storage engines - use in InnoDB-only tests
      group->adjust_end_lsn(stop_lsn, blk_len);
      set_stop_lsn(stop_lsn);
      se_sync_stop_lsn = stop_lsn;
    });

    /* Will throw error, if shutdown. We still continue
    with detach but return the error. */
    err = wait_archive_complete(se_sync_stop_lsn);
    if (!err) {
      stop_lsn = se_sync_stop_lsn;
      const auto last_log_seg_start_lsn =
          ut_uint64_align_down(stop_lsn, OS_FILE_LOG_BLOCK_SIZE);
      if (last_log_seg_start_lsn < stop_lsn) {
        ut_ad(stop_lsn <= last_log_seg_start_lsn + OS_FILE_LOG_BLOCK_SIZE);

        // In the older InnoDB-only clone implementation log archiving could
        // only stop at the end of the log. If that fell in a middle of a redo
        // log block, that block was present in the in-memory log buffer.
        //
        // With clone synchronized across storage engines, the log archiving
        // will stop at an LSN not greater than the redo log end LSN, which may
        // still be in a middle of a block. Depending on how much this LSN is
        // behind the current LSN, the needed block might be on disk, in memory,
        // or be in a middle of move from memory to disk. Handle all those cases
        // uniformly by forcing the write of this block, and reading it from the
        // disk like any older block.
        const auto needs_log_write =
            log_sys->write_lsn.load(std::memory_order_acquire) < stop_lsn;
        if (needs_log_write) log_write_up_to(*log_sys, stop_lsn, false);

        const auto last_log_seg_end_lsn =
            last_log_seg_start_lsn + OS_FILE_LOG_BLOCK_SIZE;
        byte last_log_seg_buf[OS_FILE_LOG_BLOCK_SIZE];
        recv_read_log_seg(*log_sys, &last_log_seg_buf[0],
                          last_log_seg_start_lsn, last_log_seg_end_lsn, true);

        // Roughly mimicking log_buffer_get_last_block, but without setting the
        // fields that should be already set as we read the actual valid block
        // from disk.
        const auto data_len = stop_lsn % OS_FILE_LOG_BLOCK_SIZE;
        ut_ad(data_len >= LOG_BLOCK_HDR_SIZE);
        std::memcpy(log_blk, last_log_seg_buf, data_len);
        // Our redo block copy must not include any records past the log
        // archiving stop LSN.
        std::memset(log_blk + data_len, 0x00,
                    OS_FILE_LOG_BLOCK_SIZE - data_len);
        log_block_set_data_len(log_blk, data_len);
        ut_ad(log_block_get_first_rec_group(log_blk) <= data_len);
        log_block_store_checksum(log_blk);
        blk_len = OS_FILE_LOG_BLOCK_SIZE;
      }
    }
  }

  arch_mutex_enter();

  // Relaxed store will be ordered by the arch_mutex release. This is fine as
  // all the reads from any non-master threads later will be after arch_mutex
  // acquisition.
  m_stop_lsn.store(LSN_MAX, std::memory_order_relaxed);

  if (m_state == ARCH_STATE_READ_ONLY) {
    arch_mutex_exit();
    return 0;
  }

  auto count_active_client = group->detach(stop_lsn, nullptr);
  ut_ad(group->is_referenced());

  if (!group->is_active() && err == 0) {
    /* Archiving for the group has already stopped. */
    my_error(ER_INTERNAL_ERROR, MYF(0), "Clone: Log Archiver failed");
    err = ER_INTERNAL_ERROR;
  }

  if (group->is_active() && count_active_client == 0) {
    /* No other active client. Prepare to get idle. */
    log_writer_mutex_enter(*log_sys);

    if (m_state == ARCH_STATE_ACTIVE) {
      /* The active group must be the current group. */
      ut_ad(group == m_current_group);
      m_state = ARCH_STATE_PREPARE_IDLE;
      os_event_set(log_archiver_thread_event);
    }

    log_writer_mutex_exit(*log_sys);
  }

  arch_mutex_exit();

  return (err);
}

/** Force to abort archiving (state becomes ARCH_STATE_IDLE). */
void Arch_Log_Sys::force_abort() {
  lsn_t lsn_max = LSN_MAX; /* unused */
  uint to_archive = 0;     /* unused */
  check_set_state(true, &lsn_max, &to_archive);
}

/** Release the current group from client.
@param[in]	group		group the client is attached to
@param[in]	is_durable	if client needs durable archiving */
void Arch_Log_Sys::release(Arch_Group *group, bool is_durable) {
  arch_mutex_enter();

  group->release(is_durable);

  /* Check if there are other references or archiving is still
  in progress. */
  if (group->is_referenced() || group->is_active()) {
    arch_mutex_exit();
    return;
  }

  /* Cleanup the group. */
  ut_ad(group != m_current_group);

  m_group_list.remove(group);

  ut::delete_(group);

  arch_mutex_exit();
}

/** Check and set log archive system state and output the
amount of redo log available for archiving.
@param[in]	is_abort	need to abort
@param[in,out]	archived_lsn	LSN up to which redo log is archived
@param[out]	to_archive	amount of redo log to be archived */
Arch_State Arch_Log_Sys::check_set_state(bool is_abort, lsn_t *archived_lsn,
                                         uint *to_archive) {
  auto is_shutdown = (srv_shutdown_state.load() == SRV_SHUTDOWN_LAST_PHASE ||
                      srv_shutdown_state.load() == SRV_SHUTDOWN_EXIT_THREADS);

  auto need_to_abort = (is_abort || is_shutdown);

  *to_archive = 0;

  arch_mutex_enter();

  switch (m_state) {
    case ARCH_STATE_ACTIVE:

      if (*archived_lsn != LSN_MAX) {
        /* Update system archived LSN from input */
        ut_ad(*archived_lsn >= m_archived_lsn.load());
        m_archived_lsn.store(*archived_lsn);
      } else {
        /* If input is not initialized,
        set from system archived LSN */
        *archived_lsn = m_archived_lsn.load();
      }

      lsn_t lsn_diff;

      /* Check redo log data ready to archive. */
      {
#ifdef UNIV_DEBUG
        const auto local_lsn = log_get_lsn(*log_sys);
#endif
        const auto local_write_lsn = log_sys->write_lsn.load();
        const auto local_archived_lsn = m_archived_lsn.load();
        ut_ad(local_write_lsn >= local_archived_lsn);
        const auto stop_lsn = get_stop_lsn_any_thread();
        ut_ad(stop_lsn <= local_lsn || stop_lsn == LSN_MAX ||
              DBUG_EVALUATE_IF("clone_arch_log_stop_file_end", true, false));

        const auto archive_up_to_lsn = stop_lsn > local_archived_lsn
                                           ? std::min(stop_lsn, local_write_lsn)
                                           : local_archived_lsn;
        lsn_diff = archive_up_to_lsn - local_archived_lsn;
      }

      lsn_diff = ut_uint64_align_down(lsn_diff, OS_FILE_LOG_BLOCK_SIZE);

      /* Adjust archive data length if bigger than chunks size. */
      if (lsn_diff < m_chunk_size) {
        *to_archive = static_cast<uint>(lsn_diff);
      } else {
        *to_archive = m_chunk_size;
      }

      if (!need_to_abort) {
        break;
      }

      if (!is_shutdown) {
        ut_ad(is_abort);
        /* If caller asked to abort, move to prepare idle state. Archiver
        thread will move to IDLE state eventually. */
        log_writer_mutex_enter(*log_sys);
        m_state = ARCH_STATE_PREPARE_IDLE;
        log_writer_mutex_exit(*log_sys);
        break;
      }
      [[fallthrough]];

    case ARCH_STATE_PREPARE_IDLE: {
      /* No active clients. Mark the group inactive and move
      to idle state. */
      m_current_group->disable(m_archived_lsn.load());

      /* If no client reference, free the group. */
      if (!m_current_group->is_referenced()) {
        m_group_list.remove(m_current_group);

        ut::delete_(m_current_group);
      }

      m_current_group = nullptr;

      log_writer_mutex_enter(*log_sys);
      m_state = ARCH_STATE_IDLE;
      log_writer_mutex_exit(*log_sys);
    }
      [[fallthrough]];

    case ARCH_STATE_IDLE:
    case ARCH_STATE_INIT:

      /* Abort archiver thread only in case of shutdown. */
      if (is_shutdown) {
        log_writer_mutex_enter(*log_sys);
        m_state = ARCH_STATE_ABORT;
        log_writer_mutex_exit(*log_sys);
      }
      break;

    case ARCH_STATE_ABORT:
      /* We could abort archiver from log_writer when
      it is already in the aborted state (shutdown). */
      break;

    default:
      ut_ad(false);
  }

  auto ret_state = m_state;
  arch_mutex_exit();

  return (ret_state);
}

/** Copy redo log from file context to archiver files.
@param[in]	file_ctx	file context for system redo logs
@param[in]	length		data to copy in bytes
@return error code */
dberr_t Arch_Log_Sys::copy_log(Arch_File_Ctx *file_ctx, uint length) {
  dberr_t err = DB_SUCCESS;

  if (file_ctx->is_closed()) {
    /* Open system redo log file context */
    err = file_ctx->open(true, LSN_MAX, m_start_log_index, m_start_log_offset);

    if (err != DB_SUCCESS) {
      return (err);
    }
  }

  Arch_Group *curr_group;
  uint write_size;

  curr_group = arch_log_sys->get_arch_group();

  /* Copy log data into one or more files in archiver group. */
  while (length > 0) {
    ib_uint64_t len_copy;
    ib_uint64_t len_left;

    len_copy = static_cast<ib_uint64_t>(length);

    len_left = file_ctx->bytes_left();

    /* Current file is over, switch to next file. */
    if (len_left == 0) {
      err = file_ctx->open_next(LSN_MAX, LOG_FILE_HDR_SIZE);
      if (err != DB_SUCCESS) {
        return (err);
      }

      len_left = file_ctx->bytes_left();
    }

    /* Write as much as possible from current file. */
    if (len_left < len_copy) {
      write_size = static_cast<uint>(len_left);
    } else {
      write_size = length;
    }

    err =
        curr_group->write_to_file(file_ctx, nullptr, write_size, false, false);

    if (err != DB_SUCCESS) {
      return (err);
    }

    ut_ad(length >= write_size);
    length -= write_size;
  }

  return (DB_SUCCESS);
}

bool Arch_Log_Sys::wait_idle() {
  ut_ad(mutex_own(&m_mutex));

  if (m_state == ARCH_STATE_PREPARE_IDLE) {
    os_event_set(log_archiver_thread_event);
    bool is_timeout = false;
    int alert_count = 0;

    auto err = Clone_Sys::wait_default(
        [&](bool alert, bool &result) {
          ut_ad(mutex_own(&m_mutex));
          result = (m_state == ARCH_STATE_PREPARE_IDLE);

          if (srv_shutdown_state.load() >= SRV_SHUTDOWN_CLEANUP) {
            return (ER_QUERY_INTERRUPTED);
          }

          if (result) {
            os_event_set(log_archiver_thread_event);

            /* Print messages every 1 minute - default is 5 seconds. */
            if (alert && ++alert_count == 12) {
              alert_count = 0;
              ib::info(ER_IB_MSG_24) << "Log Archiving start: waiting for "
                                        "idle state.";
            }
          }
          return (0);
        },
        &m_mutex, is_timeout);

    if (err == 0 && is_timeout) {
      ut_ad(false);
      err = ER_INTERNAL_ERROR;
      ib::info(ER_IB_MSG_25) << "Log Archiving start: wait for idle state "
                                "timed out";
    }

    if (err != 0) {
      return (false);
    }
  }
  return (true);
}

/** Wait for redo log archive up to the target LSN.
We need to wait till current log sys LSN during archive stop.
@param[in]	target_lsn	target archive LSN to wait for
@return error code */
int Arch_Log_Sys::wait_archive_complete(lsn_t target_lsn) {
  ut_ad(target_lsn != LSN_MAX);

  target_lsn = ut_uint64_align_down(target_lsn, OS_FILE_LOG_BLOCK_SIZE);

  /* Check and wait for archiver thread if needed. */
  if (m_archived_lsn.load() < target_lsn) {
    os_event_set(log_archiver_thread_event);

    bool is_timeout = false;
    int alert_count = 0;

    auto err = Clone_Sys::wait_default(
        [&](bool alert, bool &result) {
          /* Read consistent state. */
          arch_mutex_enter();
          auto state = m_state;
          arch_mutex_exit();

          /* Check if we need to abort. */
          if (state == ARCH_STATE_ABORT ||
              srv_shutdown_state.load() >= SRV_SHUTDOWN_CLEANUP) {
            my_error(ER_QUERY_INTERRUPTED, MYF(0));
            return (ER_QUERY_INTERRUPTED);
          }

          if (state == ARCH_STATE_IDLE || state == ARCH_STATE_PREPARE_IDLE) {
            my_error(ER_INTERNAL_ERROR, MYF(0), "Clone: Log Archiver failed");
            return (ER_INTERNAL_ERROR);
          }

          ut_ad(state == ARCH_STATE_ACTIVE);

          /* Check if archived LSN is behind target. */
          auto archived_lsn = m_archived_lsn.load();
          result = (archived_lsn < target_lsn);

          /* Trigger flush if needed */
          auto flush = log_sys->write_lsn.load() < target_lsn;

          if (result) {
            /* More data needs to be archived. */
            os_event_set(log_archiver_thread_event);

            /* Write system redo log if needed. */
            if (flush) {
              log_write_up_to(*log_sys, target_lsn, false);
            }
            /* Print messages every 1 minute - default is 5 seconds. */
            if (alert && ++alert_count == 12) {
              alert_count = 0;
              ib::info(ER_IB_MSG_18)
                  << "Clone Log archive stop: waiting for archiver to "
                     "finish archiving log till LSN: "
                  << target_lsn << " Archived LSN: " << archived_lsn;
            }
          }
          return (0);
        },
        nullptr, is_timeout);

    if (err == 0 && is_timeout) {
      ut_ad(false);

      ib::info(ER_IB_MSG_19) << "Clone Log archive stop: "
                                "wait for Archiver timed out";

      err = ER_INTERNAL_ERROR;
      my_error(ER_INTERNAL_ERROR, MYF(0), "Clone: Log Archiver wait too long");
    }
    return (err);
  }
  return (0);
}

/** Archive accumulated redo log in current group.
This interface is for archiver background task to archive redo log
data by calling it repeatedly over time.
@param[in, out]	init		true when called the first time; it will then
                                be set to false
@param[in]	curr_ctx	system redo logs to copy data from
@param[out]	arch_lsn	LSN up to which archiving is completed
@param[out]	wait		true, if no more redo to archive
@return true, if archiving is aborted */
bool Arch_Log_Sys::archive(bool init, Arch_File_Ctx *curr_ctx, lsn_t *arch_lsn,
                           bool *wait) {
  Arch_State curr_state;
  uint32_t arch_len;

  dberr_t err = DB_SUCCESS;
  bool is_abort = false;

  /* Initialize system redo log file context first time. */
  if (init) {
    uint num_files;

    num_files = static_cast<uint>(log_sys->n_files);

    err = curr_ctx->init(srv_log_group_home_dir, nullptr, ib_logfile_basename,
                         num_files, log_sys->file_size);

    if (err != DB_SUCCESS) {
      is_abort = true;
    }
  }

  /* Find archive system state and amount of log data to archive. */
  curr_state = check_set_state(is_abort, arch_lsn, &arch_len);

  if (curr_state == ARCH_STATE_ACTIVE) {
    /* Adjust archiver length to no go beyond file end. */
    DBUG_EXECUTE_IF(
        "clone_arch_log_stop_file_end",
        // This desyncs storage engines - use in InnoDB-only tests
        if (get_stop_lsn_any_thread() == LSN_MAX) {
          // Get the end LSN of the current log file and set that as
          // the stop LSN.
          lsn_t file_stop_lsn;
          uint32_t blk_len;
          m_current_group->adjust_end_lsn(file_stop_lsn, blk_len);
          set_stop_lsn(file_stop_lsn);
        }
        // This desyncs storage engines - use in InnoDB-only tests
        m_current_group->adjust_copy_length(*arch_lsn, arch_len););

    /* Simulate archive error. */
    DBUG_EXECUTE_IF("clone_redo_no_archive", arch_len = 0;);

    if (arch_len == 0) {
      /* Nothing to archive. Need to wait. */
      *wait = true;
      return (false);
    }

    /* Copy data from system redo log files to archiver files */
    err = copy_log(curr_ctx, arch_len);

    /* Simulate archive error. */
    DBUG_EXECUTE_IF("clone_redo_archive_error", err = DB_ERROR;);

    if (err == DB_SUCCESS) {
      *arch_lsn += arch_len;
      *wait = false;
      return (false);
    }

    /* Force abort in case of an error archiving data. */
    curr_state = check_set_state(true, arch_lsn, &arch_len);
  }

  if (curr_state == ARCH_STATE_ABORT) {
    curr_ctx->close();
    return (true);
  }

  if (curr_state == ARCH_STATE_IDLE || curr_state == ARCH_STATE_INIT) {
    curr_ctx->close();
    *arch_lsn = LSN_MAX;
    *wait = true;
    return (false);
  }

  ut_ad(curr_state == ARCH_STATE_PREPARE_IDLE);
  *wait = false;
  return (false);
}
