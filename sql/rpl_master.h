#ifndef RPL_MASTER_H_INCLUDED
/* Copyright (c) 2010, 2016, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA */


#define RPL_MASTER_H_INCLUDED


#ifdef HAVE_REPLICATION

#include <map>
#include <set>

extern bool server_id_supplied;
extern int max_binlog_dump_events;
extern my_bool opt_sporadic_binlog_dump_fail;
extern my_bool opt_show_slave_auth_info;
/* Size for preallocated replication buffer size */
extern ulong rpl_event_buffer_size;
extern uint rpl_send_buffer_size;
extern std::atomic<bool> block_dump_threads;

/*
 * SlaveStats struct contains the statistics continuously sent by slaves to the
 * master
 */
typedef struct st_slave_stats {
  int server_id;
  int timestamp;
  int milli_sec_behind_master;

  st_slave_stats(uchar* packet);
  // Operator definition for strict weak ordering. Should be a function of all
  // constituents of the struct.
  bool operator<(const st_slave_stats& other) const {
    if (timestamp < other.timestamp) {
      return true;
    } else if (timestamp == other.timestamp) {
      return milli_sec_behind_master < other.milli_sec_behind_master;
    }
    return false;
  }
} SLAVE_STATS;

typedef struct st_slave_info
{
  uint32 server_id;
  uint32 rpl_recovery_rank, master_id;
  char host[HOSTNAME_LENGTH+1];
  char user[USERNAME_LENGTH+1];
  char password[MAX_PASSWORD_LENGTH+1];
  uint16 port;
  std::list<SLAVE_STATS> *slave_stats;
  THD* thd;
  bool is_raft = false;
  char server_uuid[64];
} SLAVE_INFO;

void init_slave_list();
void end_slave_list();
void init_compressed_event_cache();
void clear_compressed_event_cache();
void free_compressed_event_cache();
bool is_semi_sync_slave(THD *thd);
int store_replica_stats(THD *thd, uchar *packet, uint packet_length);
int get_current_replication_lag();
int register_slave(THD* thd, uchar* packet, uint packet_length);
void unregister_slave(THD* thd, bool only_mine, bool need_lock_slave_list);
int register_raft_followers(
    const std::unordered_map<std::string, std::string>&, bool, bool);
bool show_slave_hosts(THD* thd, bool with_raft);
void copy_slave_map(THD* thd, std::map<THD *, SLAVE_INFO> *slaves);
String *get_slave_uuid(THD *thd, String *value);
bool show_master_offset(THD* thd, snapshot_info_st &ss_info,
                        bool* need_ok);
bool show_master_status(THD* thd);
bool show_binlogs(THD* thd, bool with_gtid = false);
void kill_zombie_dump_threads(THD* thd);
void kill_all_dump_threads();

uint find_gtid_position_helper(const char* gtid_string,
                               char *log_name, my_off_t &gtid_pos);
bool find_gtid_position(THD *thd);
my_off_t find_gtid_pos_in_log(const char* log_name, const Gtid &gtid,
                              Sid_map *sid_map);

/**
  Process a COM_BINLOG_DUMP_GTID packet.

  This function parses the packet and then calls mysql_binlog_send.

  @param thd The dump thread.
  @param packet The COM_BINLOG_DUMP_GTID packet.
  @param packet_length The length of the packet in bytes.
  @retval true Error
  @retval false Success
*/
bool com_binlog_dump_gtid(THD *thd, char *packet, uint packet_length);

/**
  Process a COM_BINLOG_DUMP packet.

  This function parses the packet and then calls mysql_binlog_send.

  @param thd The dump thread.
  @param packet The COM_BINLOG_DUMP packet.
  @param packet_length The length of the packet in bytes.
  @retval true Error
  @retval false Success
*/
bool com_binlog_dump(THD *thd, char *packet, uint packet_length);

/**
  Low-level function where the dump thread iterates over the binary
  log and sends events to the slave.  This function is common for both
  COM_BINLOG_DUMP and COM_BINLOG_DUMP_GTID.

  @param thd The dump thread.

  @param log_ident The filename of the binary log, as given in the
  COM_BINLOG_DUMP[_GTID] packet.  If this is is an empty string (first
  character is '\0'), we start with the oldest binary log.

  @param pos The offset in the binary log, as given in the
  COM_BINLOG_DUMP[_GTID] packet.  This must be at least 4 and at most
  the size of the binary log file.

  @param gtid_set The gtid_set that the slave sent, or NULL if the
  protocol is COM_BINLOG_DUMP.

  @note This function will start reading at the given (filename,
  offset), or from the oldest log if filename[0]==0.  It will send all
  events from that position; but if gtid_set!=NULL, it will skip all
  events in that set.
*/
void mysql_binlog_send(THD* thd, char* log_ident, my_off_t pos,
                       const Gtid_set* gtid_set, int flags);

int reset_master(THD* thd, bool force= false);

/*
  fill_replica_statistics
    Populates the temporary table by reading from the replica statistics map.
  Input:
    thd     in: THD
    cond    in: Item
    table   out: TABLE_LIST
*/
extern int fill_replica_statistics(THD *thd, TABLE_LIST *tables, Item *cond);

#endif /* HAVE_REPLICATION */

#endif /* RPL_MASTER_H_INCLUDED */
