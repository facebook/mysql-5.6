/* Copyright (C) 2007 Google Inc.
   Copyright (c) 2008, 2016, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA */


#include "semisync_master.h"
#include "mysqld.h"
#if defined(ENABLED_DEBUG_SYNC)
#include "debug_sync.h"
#endif
#include "sql_class.h"
#include "binlog.h"
#include <fstream>

#define TIME_THOUSAND 1000
#define TIME_MILLION  1000000
#define TIME_BILLION  1000000000

/* This indicates whether semi-synchronous replication is enabled. */
char rpl_semi_sync_master_enabled;
unsigned long rpl_semi_sync_master_timeout;
char rpl_semi_sync_master_crash_if_active_trxs;
unsigned long rpl_semi_sync_master_trace_level;
char rpl_semi_sync_master_status                    = 0;
unsigned long rpl_semi_sync_master_yes_transactions = 0;
unsigned long rpl_semi_sync_master_no_transactions  = 0;
unsigned long rpl_semi_sync_master_off_times        = 0;
unsigned long rpl_semi_sync_master_timefunc_fails   = 0;
unsigned long rpl_semi_sync_master_wait_timeouts     = 0;
unsigned long rpl_semi_sync_master_wait_sessions    = 0;
unsigned long rpl_semi_sync_master_wait_pos_backtraverse = 0;
unsigned long rpl_semi_sync_master_avg_trx_wait_time = 0;
unsigned long long rpl_semi_sync_master_trx_wait_num = 0;
unsigned long rpl_semi_sync_master_avg_net_wait_time    = 0;
unsigned long long rpl_semi_sync_master_net_wait_num = 0;
unsigned long rpl_semi_sync_master_clients          = 0;
unsigned long long rpl_semi_sync_master_net_wait_time = 0;
unsigned long long rpl_semi_sync_master_trx_wait_time = 0;
char rpl_semi_sync_master_wait_no_slave = 1;
char *histogram_trx_wait_step_size = 0;
latency_histogram histogram_trx_wait;
SHOW_VAR latency_histogram_trx_wait[NUMBER_OF_HISTOGRAM_BINS + 1];
ulonglong histogram_trx_wait_values[NUMBER_OF_HISTOGRAM_BINS];
char *rpl_semi_sync_master_whitelist = 0;


static int getWaitTime(const struct timespec& start_ts);

static unsigned long long timespec_to_usec(const struct timespec *ts)
{
#ifdef HAVE_STRUCT_TIMESPEC
  return (unsigned long long) ts->tv_sec * TIME_MILLION + ts->tv_nsec / TIME_THOUSAND;
#else
  return ts->tv.i64 / 10;
#endif /* __WIN__ */
}

/*******************************************************************************
 *
 * <ActiveTranx> class : manage all active transaction nodes
 *
 ******************************************************************************/

ActiveTranx::ActiveTranx(mysql_mutex_t *lock,
			 unsigned long trace_level)
  : Trace(trace_level), allocator_(max_connections),
    num_entries_(max_connections << 1), /* Transaction hash table size
                                         * is set to double the size
                                         * of max_connections */
    lock_(lock)
{
  /* No transactions are in the list initially. */
  trx_front_ = NULL;
  trx_rear_  = NULL;

  /* Create the hash table to find a transaction's ending event. */
  trx_htb_ = new TranxNode *[num_entries_];
  for (int idx = 0; idx < num_entries_; ++idx)
    trx_htb_[idx] = NULL;

  sql_print_information("Semi-sync replication initialized for transactions.");
}

ActiveTranx::~ActiveTranx()
{
  delete [] trx_htb_;
  trx_htb_          = NULL;
  num_entries_      = 0;
}

unsigned int ActiveTranx::calc_hash(const unsigned char *key,
                                    unsigned int length)
{
  unsigned int nr = 1, nr2 = 4;

  /* The hash implementation comes from calc_hashnr() in mysys/hash.c. */
  while (length--)
  {
    nr  ^= (((nr & 63)+nr2)*((unsigned int) (unsigned char) *key++))+ (nr << 8);
    nr2 += 3;
  }
  return((unsigned int) nr);
}

unsigned int ActiveTranx::get_hash_value(const char *log_file_name,
				 my_off_t    log_file_pos)
{
  unsigned int hash1 = calc_hash((const unsigned char *)log_file_name,
                                 strlen(log_file_name));
  unsigned int hash2 = calc_hash((const unsigned char *)(&log_file_pos),
                                 sizeof(log_file_pos));

  return (hash1 + hash2) % num_entries_;
}

int ActiveTranx::compare(const char *log_file_name1, my_off_t log_file_pos1,
			 const char *log_file_name2, my_off_t log_file_pos2)
{
  int cmp_len = strlen(log_file_name1) - strlen(log_file_name2);
  if (cmp_len)
    return cmp_len;

  int cmp = strcmp(log_file_name1, log_file_name2);

  if (cmp != 0)
    return cmp;

  if (log_file_pos1 > log_file_pos2)
    return 1;
  else if (log_file_pos1 < log_file_pos2)
    return -1;
  return 0;
}

int ActiveTranx::insert_tranx_node(const char *log_file_name,
				   my_off_t log_file_pos)
{
  const char *kWho = "ActiveTranx:insert_tranx_node";
  TranxNode  *ins_node;
  int         result = 0;
  unsigned int        hash_val;

  function_enter(kWho);

  ins_node = allocator_.allocate_node();
  if (!ins_node)
  {
    sql_print_error("%s: transaction node allocation failed for: (%s, %lu)",
                    kWho, log_file_name, (unsigned long)log_file_pos);
    result = -1;
    goto l_end;
  }

  /* insert the binlog position in the active transaction list. */
  strncpy(ins_node->log_name_, log_file_name, FN_REFLEN-1);
  ins_node->log_name_[FN_REFLEN-1] = 0; /* make sure it ends properly */
  ins_node->log_pos_ = log_file_pos;

  if (!trx_front_)
  {
    /* The list is empty. */
    trx_front_ = trx_rear_ = ins_node;
  }
  else
  {
    int cmp = compare(ins_node, trx_rear_);
    if (cmp > 0)
    {
      /* Compare with the tail first.  If the transaction happens later in
       * binlog, then make it the new tail.
       */
      trx_rear_->next_ = ins_node;
      trx_rear_        = ins_node;
    }
    else
    {
      /* Otherwise, it is an error because the transaction should hold the
       * mysql_bin_log.LOCK_log when appending events.
       */
      sql_print_error("%s: binlog write out-of-order, tail (%s, %lu), "
                      "new node (%s, %lu)", kWho,
                      trx_rear_->log_name_, (unsigned long)trx_rear_->log_pos_,
                      ins_node->log_name_, (unsigned long)ins_node->log_pos_);
      result = -1;
      goto l_end;
    }
  }

  hash_val = get_hash_value(ins_node->log_name_, ins_node->log_pos_);
  ins_node->hash_next_ = trx_htb_[hash_val];
  trx_htb_[hash_val]   = ins_node;

  if (trace_level_ & kTraceDetail)
    sql_print_information("%s: insert (%s, %lu) in entry(%u)", kWho,
                          ins_node->log_name_, (unsigned long)ins_node->log_pos_,
                          hash_val);

 l_end:
  return function_exit(kWho, result);
}

bool ActiveTranx::is_tranx_end_pos(const char *log_file_name,
				   my_off_t    log_file_pos)
{
  const char *kWho = "ActiveTranx::is_tranx_end_pos";
  function_enter(kWho);

  unsigned int hash_val = get_hash_value(log_file_name, log_file_pos);
  TranxNode *entry = trx_htb_[hash_val];

  while (entry != NULL)
  {
    if (compare(entry, log_file_name, log_file_pos) == 0)
      break;

    entry = entry->hash_next_;
  }

  if (trace_level_ & kTraceDetail)
    sql_print_information("%s: probe (%s, %lu) in entry(%u)", kWho,
                          log_file_name, (unsigned long)log_file_pos, hash_val);

  function_exit(kWho, (entry != NULL));
  return (entry != NULL);
}

int ActiveTranx::signal_waiting_sessions_all()
{
  const char *kWho = "ActiveTranx::signal_waiting_sessions_all";
  function_enter(kWho);
  for (TranxNode* entry= trx_front_; entry; entry=entry->next_)
    mysql_cond_broadcast(&entry->cond);

  return function_exit(kWho, 0);
}

int ActiveTranx::signal_waiting_sessions_up_to(const char *log_file_name,
                                               my_off_t log_file_pos)
{
  const char *kWho = "ActiveTranx::signal_waiting_sessions_up_to";
  function_enter(kWho);

  TranxNode* entry= trx_front_;
  int cmp= ActiveTranx::compare(entry->log_name_, entry->log_pos_, log_file_name, log_file_pos) ;
  while (entry && cmp <= 0)
  {
    mysql_cond_broadcast(&entry->cond);
    entry= entry->next_;
    if (entry)
      cmp= ActiveTranx::compare(entry->log_name_, entry->log_pos_, log_file_name, log_file_pos) ;
  }

  return function_exit(kWho, (entry != NULL));
}

TranxNode * ActiveTranx::find_active_tranx_node(const char *log_file_name,
                                                my_off_t log_file_pos)
{
  const char *kWho = "ActiveTranx::find_active_tranx_node";
  function_enter(kWho);

  TranxNode* entry= trx_front_;

  while (entry)
  {
    if (ActiveTranx::compare(log_file_name, log_file_pos, entry->log_name_,
                             entry->log_pos_) <= 0)
      break;
    entry= entry->next_;
  }
  function_exit(kWho, 0);
  return entry;
}

int ActiveTranx::clear_active_tranx_nodes(const char *log_file_name,
					  my_off_t log_file_pos)
{
  const char *kWho = "ActiveTranx::::clear_active_tranx_nodes";
  TranxNode *new_front;

  function_enter(kWho);

  if (log_file_name != NULL)
  {
    new_front = trx_front_;

    while (new_front)
    {
      if (compare(new_front, log_file_name, log_file_pos) > 0 ||
          new_front->n_waiters > 0)
        break;
      new_front = new_front->next_;
    }
  }
  else
  {
    /* If log_file_name is NULL, clear everything. */
    new_front = NULL;
  }

  if (new_front == NULL)
  {
    /* No active transaction nodes after the call. */

    /* Clear the hash table. */
    memset(trx_htb_, 0, num_entries_ * sizeof(TranxNode *));
    allocator_.free_all_nodes();

    /* Clear the active transaction list. */
    if (trx_front_ != NULL)
    {
      trx_front_ = NULL;
      trx_rear_  = NULL;
    }

    if (trace_level_ & kTraceDetail)
      sql_print_information("%s: cleared all nodes", kWho);
  }
  else if (new_front != trx_front_)
  {
    TranxNode *curr_node, *next_node;

    /* Delete all transaction nodes before the confirmation point. */
    int n_frees = 0;
    curr_node = trx_front_;
    while (curr_node != new_front)
    {
      next_node = curr_node->next_;
      n_frees++;

      /* Remove the node from the hash table. */
      unsigned int hash_val = get_hash_value(curr_node->log_name_, curr_node->log_pos_);
      TranxNode **hash_ptr = &(trx_htb_[hash_val]);
      while ((*hash_ptr) != NULL)
      {
        if ((*hash_ptr) == curr_node)
	{
          (*hash_ptr) = curr_node->hash_next_;
          break;
        }
        hash_ptr = &((*hash_ptr)->hash_next_);
      }

      curr_node = next_node;
    }

    trx_front_ = new_front;
    allocator_.free_nodes_before(trx_front_);

    if (trace_level_ & kTraceDetail)
      sql_print_information("%s: cleared %d nodes back until pos (%s, %lu)",
                            kWho, n_frees,
                            trx_front_->log_name_, (unsigned long)trx_front_->log_pos_);
  }

  return function_exit(kWho, 0);
}


/*******************************************************************************
 *
 * <ReplSemiSyncMaster> class: the basic code layer for sync-replication master.
 * <ReplSemiSyncSlave>  class: the basic code layer for sync-replication slave.
 *
 * The most important functions during semi-syn replication listed:
 *
 * Master:
 *  . reportReplyBinlog():  called by the binlog dump thread when it receives
 *                          the slave's status information.
 *  . updateSyncHeader():   based on transaction waiting information, decide
 *                          whether to request the slave to reply.
 *  . writeTranxInBinlog(): called by the transaction thread when it finishes
 *                          writing all transaction events in binlog.
 *  . commitTrx():          transaction thread wait for the slave reply.
 *
 * Slave:
 *  . slaveReadSyncHeader(): read the semi-sync header from the master, get the
 *                           sync status and get the payload for events.
 *  . slaveReply():          reply to the master about the replication progress.
 *
 ******************************************************************************/

ReplSemiSyncMaster::ReplSemiSyncMaster()
  : active_tranxs_(NULL),
    init_done_(false),
    reply_file_name_inited_(false),
    reply_file_pos_(0L),
    wait_file_name_inited_(false),
    wait_file_pos_(0),
    master_enabled_(false),
    wait_timeout_(0L),
    state_(0)
{
  strcpy(reply_file_name_, "");
  strcpy(wait_file_name_, "");
}

int ReplSemiSyncMaster::initObject()
{
  int result;
  const char *kWho = "ReplSemiSyncMaster::initObject";

  if (init_done_)
  {
    fprintf(stderr, "%s called twice\n", kWho);
    return 1;
  }
  init_done_ = true;

  /* References to the parameter works after set_options(). */
  setWaitTimeout(rpl_semi_sync_master_timeout);
  setTraceLevel(rpl_semi_sync_master_trace_level);

  /* Mutex initialization can only be done after MY_INIT(). */
  mysql_mutex_init(key_ss_mutex_LOCK_binlog_,
                   &LOCK_binlog_, MY_MUTEX_INIT_FAST);

  if (rpl_semi_sync_master_enabled)
    result = enableMaster();
  else
    result = disableMaster();

  if (result == 0)
  {
    result = init_whitelist();
  }

  latency_histogram_init(&histogram_trx_wait, histogram_trx_wait_step_size);
  return result;
}

int ReplSemiSyncMaster::enableMaster()
{
  int result = 0;

  /* Must have the lock when we do enable of disable. */
  lock();

  if (!getMasterEnabled())
  {
    if (active_tranxs_ == NULL)
      active_tranxs_ = new ActiveTranx(&LOCK_binlog_, trace_level_);

    if (active_tranxs_ != NULL)
    {
      commit_file_name_inited_ = false;
      reply_file_name_inited_  = false;
      wait_file_name_inited_   = false;

      set_master_enabled(true);
      state_ = true;
      sql_print_information("Semi-sync replication enabled on the master.");
    }
    else
    {
      sql_print_error("Cannot allocate memory to enable semi-sync on the master.");
      result = -1;
    }
  }

  unlock();

  return result;
}

int ReplSemiSyncMaster::disableMaster()
{
  /* Must have the lock when we do enable of disable. */
  lock();

  if (getMasterEnabled())
  {
    /* Switch off the semi-sync first so that waiting transaction will be
     * waken up.
     */
    switch_off();

    if ( active_tranxs_ && active_tranxs_->is_empty())
    {
      delete active_tranxs_;
      active_tranxs_ = NULL;
    }

    reply_file_name_inited_ = false;
    wait_file_name_inited_  = false;
    commit_file_name_inited_ = false;

    set_master_enabled(false);
    sql_print_information("Semi-sync replication disabled on the master.");
  }

  unlock();

  return 0;
}

int ReplSemiSyncMaster::init_whitelist()
{
  std::lock_guard<std::mutex> guard(rpl_semi_sync_master_whitelist_set_lock);

  char tmp_file_path[FN_REFLEN];
  char info_file_path[FN_REFLEN];
  fn_format(tmp_file_path, SEMI_SYNC_MASTER_WHITELIST_TMP_FILE.c_str(),
            mysql_data_home, "", MY_UNPACK_FILENAME|MY_RETURN_REAL_PATH);
  fn_format(info_file_path, SEMI_SYNC_MASTER_WHITELIST_FILE.c_str(),
            mysql_data_home, "", MY_UNPACK_FILENAME|MY_RETURN_REAL_PATH);

  // case: whitelist file does not exists, we're done
  if (my_access(info_file_path, F_OK))
  {
    return 0;
  }

  // case: temp file exists, delete it
  if (!my_access(tmp_file_path, F_OK) && my_delete(tmp_file_path, MYF(MY_WME)))
  {
    sql_print_error("Semi-sync master: Failed to delete tmp whitelist file.");
    return 1;
  }

  std::ifstream info_file(info_file_path, std::ofstream::in);
  if (!info_file.is_open())
  {
    sql_print_error("Semi-sync master: Could not open whitelist file: %s",
                    info_file_path);
    return 1;
  }

  std::string wlist, checksum_str;
  // first line contains the whitelist
  std::getline(info_file, wlist);
  // second line constains the checksum
  std::getline(info_file, checksum_str);
#ifndef DEBUG_OFF
  std::string tmp;
  // assert: no more lines in the file
  DBUG_ASSERT(!std::getline(info_file, tmp));
#endif
  info_file.close();

  // verify the checksum
  ulong checksum= std::strtoul(checksum_str.c_str(), nullptr, 10);
  ulong checksum_calc= crc32(0L, reinterpret_cast<const uchar*>(wlist.data()),
                             wlist.length());

  if (checksum != checksum_calc)
  {
    sql_print_error("Semi-sync master: Whitelist checksum did not match!");
    return 1;
  }
  // all good now, let's store the value in the data-structure and the sysvar
  rpl_semi_sync_master_whitelist_set= split_into_set(wlist, ',');
  // remove ANY from set
  rpl_semi_sync_master_whitelist_set.erase("ANY");
  // store the value in the sysvar, hacky but works!
  // NOTE: The plugin infrastructure will fill the default value (ANY) in the
  // variable, so we have to free that first and then update it with what we
  // found in the file
  DBUG_ASSERT(rpl_semi_sync_master_whitelist &&
              strcmp(rpl_semi_sync_master_whitelist, "ANY") == 0);
  my_free(rpl_semi_sync_master_whitelist);
  rpl_semi_sync_master_whitelist = my_strdup(wlist.c_str(), MYF(MY_WME));

  sql_print_information("Semi-sync master: Whitelist = %s",
                        rpl_semi_sync_master_whitelist);

  return 0;
}


void ReplSemiSyncMaster::cleanup()
{
  free_latency_histogram_sysvars(latency_histogram_trx_wait);
  if (init_done_)
  {
    mysql_mutex_destroy(&LOCK_binlog_);
    init_done_= false;
  }

  delete active_tranxs_;
}

void ReplSemiSyncMaster::lock()
{
  mysql_mutex_lock(&LOCK_binlog_);
}

void ReplSemiSyncMaster::unlock()
{
  mysql_mutex_unlock(&LOCK_binlog_);
}

void ReplSemiSyncMaster::add_slave()
{
  lock();
  rpl_semi_sync_master_clients++;
  DBUG_ASSERT(current_thd->semisync_whitelist_ver == 0);
  // case: if whitelist is ANY was init the version with 1 to avoid the initial
  // check in @verify_againt_whitelist()
  rpl_semi_sync_master_whitelist_set_lock.lock();
  if (strcmp(rpl_semi_sync_master_whitelist, "ANY") == 0)
  {
    current_thd->semisync_whitelist_ver= 1;
  }
  rpl_semi_sync_master_whitelist_set_lock.unlock();
  unlock();
}

void ReplSemiSyncMaster::remove_slave()
{
  lock();
  rpl_semi_sync_master_clients--;

  /* Only switch off if semi-sync is enabled and is on */
  if (getMasterEnabled() && is_on())
  {
    /* If user has chosen not to wait if no semi-sync slave available
       and the last semi-sync slave exits, turn off semi-sync on master
       immediately.
     */
    if (!rpl_semi_sync_master_wait_no_slave &&
        rpl_semi_sync_master_clients == 0)
      switch_off();
  }
  unlock();
}

bool ReplSemiSyncMaster::is_semi_sync_slave()
{
  int null_value;
  long long val= 0;
  get_user_var_int("rpl_semi_sync_slave", &val, &null_value);
  return val;
}

// This method was copied from get_slave_uuid() in rpl_master.cc
std::string ReplSemiSyncMaster::get_slave_uuid() const
{
  const uchar name[] = "slave_uuid";
  const THD *thd = current_thd;

  user_var_entry *entry =
    (user_var_entry*) my_hash_search(&thd->user_vars, name, sizeof(name) - 1);

  if (entry && entry->length() > 0)
    return std::string(entry->ptr(), entry->length());

  return std::string();
}

bool ReplSemiSyncMaster::update_whitelist(std::string& wlist)
{
  std::lock_guard<std::mutex> guard(rpl_semi_sync_master_whitelist_set_lock);

  // remove all spaces
  wlist.erase(std::remove(wlist.begin(), wlist.end(), ' '), wlist.end());

  std::unordered_set<std::string> local_whitelist_set;
  local_whitelist_set = rpl_semi_sync_master_whitelist_set;

  // case: add a single uuid to the whitelist (value starts with +)
  if (wlist[0] == '+')
  {
    const auto str= wlist.substr(1);
    // case: +ANY specified, start with a clean slate
    if (str == "ANY")
    {
      local_whitelist_set.clear();
    }
    local_whitelist_set.insert(str);
  }
  // case: remove a single uuid to the whitelist (value starts with -)
  else if (wlist[0] == '-')
  {
    const auto str= wlist.substr(1);
    // case: -ANY specified, start with a clean slate
    if (str == "ANY")
    {
      local_whitelist_set.clear();
    }
    local_whitelist_set.erase(str);
  }
  // case: full comma separated string is specified
  else
  {
    local_whitelist_set = split_into_set(wlist, ',');
  }

  // re-calculate wlist from the set
  wlist = "";
  for (const auto uuid : local_whitelist_set)
  {
    wlist.append(uuid);
    wlist.append(",");
  }
  // remove last comma
  if (!wlist.empty())
  {
    wlist.pop_back();
  }

  // calculate CRC32 checksum of the whitelist
  const ulong checksum= crc32(0L, reinterpret_cast<const uchar*>(wlist.data()),
                              wlist.length());

  char tmp_file_path[FN_REFLEN];
  char info_file_path[FN_REFLEN];
  fn_format(tmp_file_path, SEMI_SYNC_MASTER_WHITELIST_TMP_FILE.c_str(),
            mysql_data_home, "", MY_UNPACK_FILENAME|MY_RETURN_REAL_PATH);
  fn_format(info_file_path, SEMI_SYNC_MASTER_WHITELIST_FILE.c_str(),
            mysql_data_home, "", MY_UNPACK_FILENAME|MY_RETURN_REAL_PATH);

  std::ofstream tmp_info_file(tmp_file_path, std::ofstream::out);
  if (!tmp_info_file.is_open())
  {
    sql_print_error("Semi-sync master: Failed to open whitelist tmp file");
    return false;
  }
  tmp_info_file << wlist << std::endl << checksum;
  tmp_info_file.close();

  if (std::rename(tmp_file_path, info_file_path))
  {
    sql_print_error("Semi-sync master: Failed to rename whitelist tmp file");
    return false;
  }
  rpl_semi_sync_master_whitelist_set = local_whitelist_set;
  // remove ANY from set
  rpl_semi_sync_master_whitelist_set.erase("ANY");
  // NOTE: make sure to change the version only after the updating the whitelist
  ++rpl_semi_sync_master_whitelist_ver;

  sql_print_information("Semi-sync master: Whitelist updated from %s to %s",
                        rpl_semi_sync_master_whitelist, wlist.c_str());

  return true;
}

bool ReplSemiSyncMaster::verify_against_whitelist()
{
  auto local_whitelist_ver= rpl_semi_sync_master_whitelist_ver.load();

  // case: the current threads version is out-dated, so we have to check the
  // whitelist
  if (current_thd->semisync_whitelist_ver < local_whitelist_ver)
  {
    const auto& slave_uuid = get_slave_uuid();

    std::lock_guard<std::mutex> guard(rpl_semi_sync_master_whitelist_set_lock);

    // case: whitelist is enabled and this slave is not in whitelist
    if (strcmp(rpl_semi_sync_master_whitelist, "ANY") != 0 &&
        rpl_semi_sync_master_whitelist_set.find(slave_uuid) ==
        rpl_semi_sync_master_whitelist_set.end())
    {
      sql_print_error("Semi-sync master: Received an ACK from an "
                      "unrecognized slave with UUID = %s, ignoring",
                      slave_uuid.c_str());
      return false;
    }
    // case: update the threads whitelist version
    else
    {
      current_thd->semisync_whitelist_ver = local_whitelist_ver;
    }
  }
#ifndef DBUG_OFF
  else
  {
    DBUG_ASSERT(current_thd->semisync_whitelist_ver == local_whitelist_ver);
  }
#endif
  return true;
}

int ReplSemiSyncMaster::reportReplyBinlog(uint32 server_id,
                                          const char *log_file_name,
                                          my_off_t log_file_pos,
                                          bool skipped_event)
{
  const char *kWho = "ReplSemiSyncMaster::reportReplyBinlog";
  int   cmp;
  bool  can_release_threads = false;
  bool  need_copy_send_pos = true;
  int   result = 0;

  if (!(getMasterEnabled()))
    return 0;

  function_enter(kWho);

  lock();

  /* This is the real check inside the mutex. */
  if (!getMasterEnabled())
    goto l_end;

  if (!is_on())
    /* We check to see whether we can switch semi-sync ON. */
    try_switch_on(server_id, log_file_name, log_file_pos);

  /* Check if this reply came from a slave in the whitelist */
  if (!verify_against_whitelist())
  {
    result = 2;
    goto l_end;
  }

  /* The position should increase monotonically, if there is only one
   * thread sending the binlog to the slave.
   * In reality, to improve the transaction availability, we allow multiple
   * sync replication slaves.  So, if any one of them get the transaction,
   * the transaction session in the primary can move forward.
   */
  if (reply_file_name_inited_)
  {
    cmp = ActiveTranx::compare(log_file_name, log_file_pos,
                               reply_file_name_, reply_file_pos_);

    /* If the requested position is behind the sending binlog position,
     * would not adjust sending binlog position.
     * We based on the assumption that there are multiple semi-sync slave,
     * and at least one of them shou/ld be up to date.
     * If all semi-sync slaves are behind, at least initially, the primary
     * can find the situation after the waiting timeout.  After that, some
     * slaves should catch up quickly.
     */
    if (cmp < 0)
    {
      /* If the position is behind, do not copy it. */
      need_copy_send_pos = false;
    }
  }

  if (need_copy_send_pos)
  {
    strncpy(reply_file_name_, log_file_name, sizeof(reply_file_name_) - 1);
    reply_file_name_[sizeof(reply_file_name_) - 1]= '\0';
    reply_file_pos_ = log_file_pos;
    reply_file_name_inited_ = true;

#ifndef MYSQL_CLIENT
    if (rpl_wait_for_semi_sync_ack)
    {
      const LOG_POS_COORD coord { (char*) reply_file_name_, reply_file_pos_ };
      signal_semi_sync_ack(&coord);
      if (trace_level_ & kTraceDetail)
      {
        sql_print_information("[rpl_wait_for_semi_sync_ack] Signaled till: "
                              "%s:%llu", coord.file_name, coord.pos);
      }
    }
#endif

    if (trace_level_ & kTraceDetail)
    {
      if(!skipped_event)
        sql_print_information("%s: Got reply at (%s, %lu)", kWho,
                            log_file_name, (unsigned long)log_file_pos);
      else
        sql_print_information("%s: Transaction skipped at (%s, %lu)", kWho,
                            log_file_name, (unsigned long)log_file_pos);
    }
  }

  if (rpl_semi_sync_master_wait_sessions > 0)
  {
    /* Let us check if some of the waiting threads doing a trx
     * commit can now proceed.
     */
    cmp = ActiveTranx::compare(reply_file_name_, reply_file_pos_,
                               wait_file_name_, wait_file_pos_);
    if (cmp >= 0)
    {
      /* Yes, at least one waiting thread can now proceed:
       * let us release all waiting threads with a broadcast
       */
      can_release_threads = true;
      wait_file_name_inited_ = false;
    }
  }

 l_end:

  if (can_release_threads)
  {
    if (trace_level_ & kTraceDetail)
      sql_print_information("%s: signal all waiting threads.", kWho);
    active_tranxs_->signal_waiting_sessions_up_to(reply_file_name_, reply_file_pos_);
  }
  unlock();
  return function_exit(kWho, result);
}

int ReplSemiSyncMaster::commitTrx(const char* trx_wait_binlog_name,
				  my_off_t trx_wait_binlog_pos)
{
  const char *kWho = "ReplSemiSyncMaster::commitTrx";

  function_enter(kWho);
  PSI_stage_info old_stage;

#if defined(ENABLED_DEBUG_SYNC)
  /* debug sync may not be initialized for a master */
  if (current_thd->debug_sync_control)
    DEBUG_SYNC(current_thd, "rpl_semisync_master_commit_trx_before_lock");
#endif
  /* Acquire the mutex. */
  lock();

  TranxNode* entry= NULL;
  mysql_cond_t* thd_cond= NULL;
  bool is_semi_sync_trans= true;
  if (active_tranxs_ != NULL && trx_wait_binlog_name)
  {
    entry=
      active_tranxs_->find_active_tranx_node(trx_wait_binlog_name,
                                             trx_wait_binlog_pos);
    if (entry)
      thd_cond= &entry->cond;
  }
  /* This must be called after acquired the lock */
  THD_ENTER_COND(NULL, thd_cond, &LOCK_binlog_,
                 & stage_waiting_for_semi_sync_ack_from_slave,
                 & old_stage);

  if (getMasterEnabled() && trx_wait_binlog_name)
  {
    struct timespec start_ts;
    struct timespec abstime;
    int wait_result;

    set_timespec(start_ts, 0);
    /* This is the real check inside the mutex. */
    if (!getMasterEnabled() || !is_on())
      goto l_end;

    if (trace_level_ & kTraceDetail)
    {
      sql_print_information("%s: wait pos (%s, %lu), repl(%d)\n", kWho,
                            trx_wait_binlog_name, (unsigned long)trx_wait_binlog_pos,
                            (int)is_on());
    }

    /* Calcuate the waiting period. */
#ifndef HAVE_STRUCT_TIMESPEC
      abstime.tv.i64 = start_ts.tv.i64 + (__int64)wait_timeout_ * TIME_THOUSAND * 10;
      abstime.max_timeout_msec= (long)wait_timeout_;
#else
      abstime.tv_sec = start_ts.tv_sec + wait_timeout_ / TIME_THOUSAND;
      abstime.tv_nsec = start_ts.tv_nsec +
        (wait_timeout_ % TIME_THOUSAND) * TIME_MILLION;
      if (abstime.tv_nsec >= TIME_BILLION)
      {
        abstime.tv_sec++;
        abstime.tv_nsec -= TIME_BILLION;
      }
#endif /* __WIN__ */

    while (is_on() && entry)
    {
      if (reply_file_name_inited_)
      {
        int cmp = ActiveTranx::compare(reply_file_name_, reply_file_pos_,
                                       trx_wait_binlog_name, trx_wait_binlog_pos);
        if (cmp >= 0)
        {
          /* We have already sent the relevant binlog to the slave: no need to
           * wait here.
           */
          if (trace_level_ & kTraceDetail)
            sql_print_information("%s: Binlog reply is ahead (%s, %lu),",
                                  kWho, reply_file_name_, (unsigned long)reply_file_pos_);
          break;
        }
      }
      /*
        When code reaches here an Entry object may not be present in the
        following scenario.

        Semi sync was not enabled when transaction entered into ordered_commit
        process. During flush stage, semi sync was not enabled and there was no
        'Entry' object created for the transaction being committed and at a
        later stage it was enabled. In this case trx_wait_binlog_name and
        trx_wait_binlog_pos are set but the 'Entry' object is not present. Hence
        dump thread will not wait for reply from slave and it will not update
        reply_file_name. In such case the committing transaction should not wait
        for an ack from slave and it should be considered as an async
        transaction.
      */
      if (!entry)
      {
        is_semi_sync_trans= false;
        goto l_end;
      }

      /* Let us update the info about the minimum binlog position of waiting
       * threads.
       */
      if (wait_file_name_inited_)
      {
        int cmp = ActiveTranx::compare(trx_wait_binlog_name, trx_wait_binlog_pos,
                                       wait_file_name_, wait_file_pos_);
        if (cmp <= 0)
	{
          /* This thd has a lower position, let's update the minimum info. */
          strncpy(wait_file_name_, trx_wait_binlog_name, sizeof(wait_file_name_) - 1);
          wait_file_name_[sizeof(wait_file_name_) - 1]= '\0';
          wait_file_pos_ = trx_wait_binlog_pos;

          rpl_semi_sync_master_wait_pos_backtraverse++;
          if (trace_level_ & kTraceDetail)
            sql_print_information("%s: move back wait position (%s, %lu),",
                                  kWho, wait_file_name_, (unsigned long)wait_file_pos_);
        }
      }
      else
      {
        strncpy(wait_file_name_, trx_wait_binlog_name, sizeof(wait_file_name_) - 1);
        wait_file_name_[sizeof(wait_file_name_) - 1]= '\0';
        wait_file_pos_ = trx_wait_binlog_pos;
        wait_file_name_inited_ = true;

        if (trace_level_ & kTraceDetail)
          sql_print_information("%s: init wait position (%s, %lu),",
                                kWho, wait_file_name_, (unsigned long)wait_file_pos_);
      }

      /* In semi-synchronous replication, we wait until the binlog-dump
       * thread has received the reply on the relevant binlog segment from the
       * replication slave.
       *
       * Let us suspend this thread to wait on the condition;
       * when replication has progressed far enough, we will release
       * these waiting threads.
       */
      rpl_semi_sync_master_wait_sessions++;
      
      if (trace_level_ & kTraceDetail)
        sql_print_information("%s: wait %lu ms for binlog sent (%s, %lu)",
                              kWho, wait_timeout_,
                              wait_file_name_, (unsigned long)wait_file_pos_);
      
      /* wait for the position to be ACK'ed back */
      assert(entry);
      entry->n_waiters++;
      wait_result= mysql_cond_timedwait(&entry->cond, &LOCK_binlog_, &abstime);
      entry->n_waiters--;
      rpl_semi_sync_master_wait_sessions--;
      
      if (wait_result != 0)
      {
        /* This is a real wait timeout. */
        sql_print_warning("Timeout waiting for reply of binlog (file: %s, pos: %lu), "
                          "semi-sync up to file %s, position %lu.",
                          trx_wait_binlog_name, (unsigned long)trx_wait_binlog_pos,
                          reply_file_name_, (unsigned long)reply_file_pos_);
        rpl_semi_sync_master_wait_timeouts++;
        
        /* switch semi-sync off */
        switch_off();
      }
      else
      {
        int wait_time;
        
        wait_time = getWaitTime(start_ts);
        if (wait_time < 0)
        {
          if (trace_level_ & kTraceGeneral)
          {
            sql_print_information("Assessment of waiting time for commitTrx "
                                  "failed at wait position (%s, %lu)",
                                  trx_wait_binlog_name,
                                  (unsigned long)trx_wait_binlog_pos);
          }
          rpl_semi_sync_master_timefunc_fails++;
        }
        else
        {
          rpl_semi_sync_master_trx_wait_num++;
          rpl_semi_sync_master_trx_wait_time += wait_time;
          if (histogram_trx_wait_step_size)
            latency_histogram_increment(&histogram_trx_wait,
              microseconds_to_my_timer((double)wait_time), 1);
        }
      }
    }

l_end:
    /* Update the status counter. */
    if (is_on() && is_semi_sync_trans)
      rpl_semi_sync_master_yes_transactions++;
    else
      rpl_semi_sync_master_no_transactions++;

  }

  /* Last waiter removes the TranxNode */
  if (trx_wait_binlog_name && active_tranxs_
      && entry && entry->n_waiters == 0)
    active_tranxs_->clear_active_tranx_nodes(trx_wait_binlog_name,
                                             trx_wait_binlog_pos);

  /* The lock held will be released by thd_exit_cond, so no need to
    call unlock() here */
  THD_EXIT_COND(NULL, & old_stage);
  return function_exit(kWho, 0);
}

/* Indicate that semi-sync replication is OFF now.
 * 
 * What should we do when it is disabled?  The problem is that we want
 * the semi-sync replication enabled again when the slave catches up
 * later.  But, it is not that easy to detect that the slave has caught
 * up.  This is caused by the fact that MySQL's replication protocol is
 * asynchronous, meaning that if the master does not use the semi-sync
 * protocol, the slave would not send anything to the master.
 * Still, if the master is sending (N+1)-th event, we assume that it is
 * an indicator that the slave has received N-th event and earlier ones.
 *
 * If semi-sync is disabled, all transactions still update the wait
 * position with the last position in binlog.  But no transactions will
 * wait for confirmations maintained.  In binlog dump thread,
 * updateSyncHeader() checks whether the current sending event catches
 * up with last wait position.  If it does match, semi-sync will be
 * switched on again.
 */
int ReplSemiSyncMaster::switch_off()
{
  const char *kWho = "ReplSemiSyncMaster::switch_off";

  if (rpl_semi_sync_master_crash_if_active_trxs && !active_tranxs_->is_empty())
  {
    sql_print_error("Force shutdown: Semi-sync master is being switched off "
                    "while there are active un-acked transactions");
    delete_pid_file(MYF(MY_WME));
    exit(0);
  }

  function_enter(kWho);
  state_ = false;

  rpl_semi_sync_master_off_times++;
  wait_file_name_inited_   = false;
  reply_file_name_inited_  = false;
  sql_print_information("Semi-sync replication switched OFF.");

  /* signal waiting sessions */
  active_tranxs_->signal_waiting_sessions_all();

  return function_exit(kWho, 0);
}

int ReplSemiSyncMaster::try_switch_on(int server_id,
				      const char *log_file_name,
				      my_off_t log_file_pos)
{
  const char *kWho = "ReplSemiSyncMaster::try_switch_on";
  bool semi_sync_on = false;

  function_enter(kWho);

  /* If the current sending event's position is larger than or equal to the
   * 'largest' commit transaction binlog position, the slave is already
   * catching up now and we can switch semi-sync on here.
   * If commit_file_name_inited_ indicates there are no recent transactions,
   * we can enable semi-sync immediately.
   */
  if (commit_file_name_inited_)
  {
    int cmp = ActiveTranx::compare(log_file_name, log_file_pos,
                                   commit_file_name_, commit_file_pos_);
    semi_sync_on = (cmp >= 0);
  }
  else
  {
    semi_sync_on = true;
  }

  if (semi_sync_on)
  {
    /* Switch semi-sync replication on. */
    state_ = true;

    sql_print_information("Semi-sync replication switched ON with slave (server_id: %d) "
                          "at (%s, %lu)",
                          server_id, log_file_name,
                          (unsigned long)log_file_pos);
  }

  return function_exit(kWho, 0);
}

int ReplSemiSyncMaster::reserveSyncHeader(unsigned char *header,
					  unsigned long size)
{
  const char *kWho = "ReplSemiSyncMaster::reserveSyncHeader";
  function_enter(kWho);

  int hlen=0;
  DBUG_ASSERT(is_semi_sync_slave());
  /* No enough space for the extra header, disable semi-sync master */
  if (sizeof(kSyncHeader) > size)
  {
    sql_print_warning("No enough space in the packet "
                      "for semi-sync extra header, "
                      "semi-sync replication disabled");
    disableMaster();
    return 0;
  }

  /* Set the magic number and the sync status.  By default, no sync
   * is required.
   */
  memcpy(header, kSyncHeader, sizeof(kSyncHeader));
  hlen= sizeof(kSyncHeader);
  return function_exit(kWho, hlen);
}

int ReplSemiSyncMaster::updateSyncHeader(unsigned char *packet,
					 const char *log_file_name,
					 my_off_t log_file_pos,
					 uint32 server_id)
{
  const char *kWho = "ReplSemiSyncMaster::updateSyncHeader";
  int  cmp = 0;
  bool sync = false;

  /* If the semi-sync master is not enabled, or the slave is not a semi-sync
   * target, do not request replies from the slave.
   */
  DBUG_ASSERT(is_semi_sync_slave());
  if (!getMasterEnabled())
    return 0;

  function_enter(kWho);

  lock();

  /* This is the real check inside the mutex. */
  if (!getMasterEnabled())
    goto l_end; // sync= false at this point in time

  if (is_on())
  {
    /* semi-sync is ON */
    /* sync= false; No sync unless a transaction is involved. */

    if (reply_file_name_inited_)
    {
      cmp = ActiveTranx::compare(log_file_name, log_file_pos,
                                 reply_file_name_, reply_file_pos_);
      if (cmp <= 0)
      {
        /* If we have already got the reply for the event, then we do
         * not need to sync the transaction again.
         */
        goto l_end;
      }
    }

    if (wait_file_name_inited_)
    {
      cmp = ActiveTranx::compare(log_file_name, log_file_pos,
                                 wait_file_name_, wait_file_pos_);
    }
    else
    {
      cmp = 1;
    }
    
    /* If we are already waiting for some transaction replies which
     * are later in binlog, do not wait for this one event.
     */
    if (cmp >= 0)
    {
      /* 
       * We only wait if the event is a transaction's ending event.
       */
      assert(active_tranxs_ != NULL);
      sync = active_tranxs_->is_tranx_end_pos(log_file_name,
                                               log_file_pos);
    }
  }
  else
  {
    if (commit_file_name_inited_)
    {
      int cmp = ActiveTranx::compare(log_file_name, log_file_pos,
                                     commit_file_name_, commit_file_pos_);
      sync = (cmp >= 0);
    }
    else
    {
      sync = true;
    }
  }

  if (trace_level_ & kTraceDetail)
    sql_print_information("%s: server(%d), (%s, %lu) sync(%d), repl(%d)",
                          kWho, server_id, log_file_name,
                          (unsigned long)log_file_pos, sync, (int)is_on());

 l_end:
  unlock();

  /* We do not need to clear sync flag because we set it to 0 when we
   * reserve the packet header.
   */
  if (sync)
  {
    (packet)[2] = kPacketFlagSync;
  }

  return function_exit(kWho, 0);
}

int ReplSemiSyncMaster::writeTranxInBinlog(const char* log_file_name,
					   my_off_t log_file_pos)
{
  const char *kWho = "ReplSemiSyncMaster::writeTranxInBinlog";
  int result = 0;

  function_enter(kWho);

  lock();

  /* This is the real check inside the mutex. */
  if (!getMasterEnabled())
    goto l_end;

  /* Update the 'largest' transaction commit position seen so far even
   * though semi-sync is switched off.
   * It is much better that we update commit_file_* here, instead of
   * inside commitTrx().  This is mostly because updateSyncHeader()
   * will watch for commit_file_* to decide whether to switch semi-sync
   * on. The detailed reason is explained in function updateSyncHeader().
   */
  if (commit_file_name_inited_)
  {
    int cmp = ActiveTranx::compare(log_file_name, log_file_pos,
                                   commit_file_name_, commit_file_pos_);
    if (cmp > 0)
    {
      /* This is a larger position, let's update the maximum info. */
      strncpy(commit_file_name_, log_file_name, FN_REFLEN-1);
      commit_file_name_[FN_REFLEN-1] = 0; /* make sure it ends properly */
      commit_file_pos_ = log_file_pos;
    }
  }
  else
  {
    strncpy(commit_file_name_, log_file_name, FN_REFLEN-1);
    commit_file_name_[FN_REFLEN-1] = 0; /* make sure it ends properly */
    commit_file_pos_ = log_file_pos;
    commit_file_name_inited_ = true;
  }

  if (is_on())
  {
    assert(active_tranxs_ != NULL);
    if(active_tranxs_->insert_tranx_node(log_file_name, log_file_pos))
    {
      /*
        if insert tranx_node failed, print a warning message
        and turn off semi-sync
      */
      sql_print_warning("Semi-sync failed to insert tranx_node for binlog file: %s, position: %lu",
                        log_file_name, (ulong)log_file_pos);
      switch_off();
    }
  }

 l_end:
  unlock();

  return function_exit(kWho, result);
}

int ReplSemiSyncMaster::skipSlaveReply(const char *event_buf,
                                       uint32 server_id,
                                       const char* skipped_log_file,
                                       my_off_t skipped_log_pos)
{
  const char *kWho = "ReplSemiSyncMaster::skipSlaveReply";

  function_enter(kWho);

  assert((unsigned char)event_buf[1] == kPacketMagicNum);
  if ((unsigned char)event_buf[2] != kPacketFlagSync)
  {
    /* current event would not require a reply anyway */
    goto l_end;
  }

  reportReplyBinlog(server_id, skipped_log_file,
                    skipped_log_pos, true);

 l_end:
  return function_exit(kWho, 0);
}

int ReplSemiSyncMaster::readSlaveReply(NET *net, uint32 server_id,
                                       const char *event_buf)
{
  const char *kWho = "ReplSemiSyncMaster::readSlaveReply";
  const unsigned char *packet;
  char     log_file_name[FN_REFLEN];
  my_off_t log_file_pos;
  ulong    log_file_len = 0;
  ulong    packet_len;
  int      result = -1;

  struct timespec start_ts= { 0, 0 };
  ulong trc_level = trace_level_;

  function_enter(kWho);

  assert((unsigned char)event_buf[1] == kPacketMagicNum);
  if ((unsigned char)event_buf[2] != kPacketFlagSync)
  {
    /* current event does not require reply */
    result = 0;
    goto l_end;
  }

  if (trc_level & kTraceNetWait)
    set_timespec(start_ts, 0);

  /* We flush to make sure that the current event is sent to the network,
   * instead of being buffered in the TCP/IP stack.
   */
  if (net_flush(net))
  {
    sql_print_error("Semi-sync master failed on net_flush() "
                    "before waiting for slave reply");
    goto l_end;
  }

  net_clear(net, 0);
  if (trc_level & kTraceDetail)
    sql_print_information("%s: Wait for replica's reply", kWho);

  /* Wait for the network here.  Though binlog dump thread can indefinitely wait
   * here, transactions would not wait indefintely.
   * Transactions wait on binlog replies detected by binlog dump threads.  If
   * binlog dump threads wait too long, transactions will timeout and continue.
   */
  packet_len = my_net_read(net);

  if (trc_level & kTraceNetWait)
  {
    int wait_time = getWaitTime(start_ts);
    if (wait_time < 0)
    {
      sql_print_information("Assessment of waiting time for "
                            "readSlaveReply failed.");
      rpl_semi_sync_master_timefunc_fails++;
    }
    else
    {
      rpl_semi_sync_master_net_wait_num++;
      rpl_semi_sync_master_net_wait_time += wait_time;
    }
  }

  if (packet_len == packet_error || packet_len < REPLY_BINLOG_NAME_OFFSET)
  {
    if (packet_len == packet_error)
      sql_print_error("Read semi-sync reply network error: %s (errno: %d)",
                      net->last_error, net->last_errno);
    else
      sql_print_error("Read semi-sync reply length error: %s (errno: %d)",
                      net->last_error, net->last_errno);
    goto l_end;
  }

  packet = net->read_pos;
  if (packet[REPLY_MAGIC_NUM_OFFSET] != ReplSemiSyncMaster::kPacketMagicNum)
  {
    sql_print_error("Read semi-sync reply magic number error");
    goto l_end;
  }

  log_file_pos = uint8korr(packet + REPLY_BINLOG_POS_OFFSET);
  log_file_len = packet_len - REPLY_BINLOG_NAME_OFFSET;
  if (log_file_len >= FN_REFLEN)
  {
    sql_print_error("Read semi-sync reply binlog file length too large");
    goto l_end;
  }
  strncpy(log_file_name, (const char*)packet + REPLY_BINLOG_NAME_OFFSET, log_file_len);
  log_file_name[log_file_len] = 0;

  if (trc_level & kTraceDetail)
    sql_print_information("%s: Got reply (%s, %lu)",
                          kWho, log_file_name, (ulong)log_file_pos);

  result = reportReplyBinlog(server_id, log_file_name, log_file_pos);

 l_end:
  return function_exit(kWho, result);
}


int ReplSemiSyncMaster::resetMaster()
{
  const char *kWho = "ReplSemiSyncMaster::resetMaster";
  int result = 0;

  function_enter(kWho);


  lock();

  state_ = getMasterEnabled()? 1 : 0;

  wait_file_name_inited_   = false;
  reply_file_name_inited_  = false;
  commit_file_name_inited_ = false;

  rpl_semi_sync_master_yes_transactions = 0;
  rpl_semi_sync_master_no_transactions = 0;
  rpl_semi_sync_master_off_times = 0;
  rpl_semi_sync_master_timefunc_fails = 0;
  rpl_semi_sync_master_wait_sessions = 0;
  rpl_semi_sync_master_wait_pos_backtraverse = 0;
  rpl_semi_sync_master_trx_wait_num = 0;
  rpl_semi_sync_master_trx_wait_time = 0;
  rpl_semi_sync_master_net_wait_num = 0;
  rpl_semi_sync_master_net_wait_time = 0;

  unlock();

  return function_exit(kWho, result);
}

void ReplSemiSyncMaster::setExportStats()
{
  lock();

  rpl_semi_sync_master_status           = state_;
  rpl_semi_sync_master_avg_trx_wait_time=
    ((rpl_semi_sync_master_trx_wait_num) ?
     (unsigned long)((double)rpl_semi_sync_master_trx_wait_time /
                     ((double)rpl_semi_sync_master_trx_wait_num)) : 0);
  rpl_semi_sync_master_avg_net_wait_time=
    ((rpl_semi_sync_master_net_wait_num) ?
     (unsigned long)((double)rpl_semi_sync_master_net_wait_time /
                     ((double)rpl_semi_sync_master_net_wait_num)) : 0);

  for (size_t i_bins = 0; i_bins < NUMBER_OF_HISTOGRAM_BINS; ++i_bins) {
    histogram_trx_wait_values[i_bins] =
      latency_histogram_get_count(&histogram_trx_wait, i_bins);
  }

  unlock();
}

/* Get the waiting time given the wait's staring time.
 * 
 * Return:
 *  >= 0: the waiting time in microsecons(us)
 *   < 0: error in get time or time back traverse
 */
static int getWaitTime(const struct timespec& start_ts)
{
  unsigned long long start_usecs, end_usecs;
  struct timespec end_ts;
  
  /* Starting time in microseconds(us). */
  start_usecs = timespec_to_usec(&start_ts);

  /* Get the wait time interval. */
  set_timespec(end_ts, 0);

  /* Ending time in microseconds(us). */
  end_usecs = timespec_to_usec(&end_ts);

  if (end_usecs < start_usecs)
    return -1;

  return (int)(end_usecs - start_usecs);
}

void
ReplSemiSyncMaster::update_histogram_trx_wait_step_size(const char *step_size)
{
  lock();
  latency_histogram_init(&histogram_trx_wait, step_size);
  unlock();
}
