/* Copyright (c) 2008, 2014, Oracle and/or its affiliates. All rights reserved.

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

#ifndef REPLICATION_H
#define REPLICATION_H

#include <mysql.h>
#include "binlog.h"

typedef struct st_mysql MYSQL;

#ifdef __cplusplus
extern "C" {
#endif

/**
   Transaction observer flags.
*/
enum Trans_flags {
  /** Transaction is a real transaction */
  TRANS_IS_REAL_TRANS = 1
};

/**
   Transaction observer parameter
*/
typedef struct Trans_param {
  uint32 server_id;
  const char* host_or_ip;
  uint32 flags;

  /*
    The latest binary log file name and position written by current
    transaction, if binary log is disabled or no log event has been
    written into binary log file by current transaction (events
    written into transaction log cache are not counted), these two
    member will be zero.
  */
  const char *log_file;
  my_off_t log_pos;
} Trans_param;

/**
   Observes and extends transaction execution
*/
typedef struct Trans_observer {
  uint32 len;

  /**
     This callback is called before transaction commit
     and after binlog sync.

     For both non-transactional tables and transactional
     tables this is called after binlog sync.

     @param param The parameter for transaction observers

     @retval 0 Sucess
     @retval 1 Failure
  */
  int (*before_commit)(Trans_param *param);
  /**
     This callback is called after transaction commit
     
     This callback is called right after commit to storage engines for
     transactional tables.

     For non-transactional tables, this is called at the end of the
     statement, before sending statement status, if the statement
     succeeded.

     @note The return value is currently ignored by the server.

     @param param The parameter for transaction observers

     @retval 0 Sucess
     @retval 1 Failure
  */
  int (*after_commit)(Trans_param *param);

  /**
     This callback is called after transaction rollback

     This callback is called right after rollback to storage engines
     for transactional tables.

     For non-transactional tables, this is called at the end of the
     statement, before sending statement status, if the statement
     failed.

     @note The return value is currently ignored by the server.

     @param param The parameter for transaction observers

     @retval 0 Sucess
     @retval 1 Failure
  */
  int (*after_rollback)(Trans_param *param);
} Trans_observer;

/**
   Binlog storage flags
*/
enum Binlog_storage_flags {
  /** Binary log was sync:ed */
  BINLOG_STORAGE_IS_SYNCED = 1
};

/**
   Binlog storage observer parameters
 */
typedef struct Binlog_storage_param {
  uint32 server_id;
  const char* host_or_ip;
} Binlog_storage_param;

/**
   Observe binlog logging storage
*/
typedef struct Binlog_storage_observer {
  uint32 len;

  /**
     This callback is called after binlog has been flushed

     This callback is called after cached events have been flushed to
     binary log file but not yet synced.

     @param param Observer common parameter
     @param log_file Binlog file name been updated
     @param log_pos Binlog position after update

     @retval 0 Sucess
     @retval 1 Failure
  */
  int (*after_flush)(Binlog_storage_param *param,
                     const char *log_file, my_off_t log_pos);
} Binlog_storage_observer;

/**
   Replication binlog transmitter (binlog dump) observer parameter.
*/
typedef struct Binlog_transmit_param {
  uint32 server_id;
  const char *host_or_ip;
  uint32 flags;
  /* Let us keep 1-16 as output flags and 17-32 as input flags */
  static const uint32 F_OBSERVE= 1;
  static const uint32 F_DONT_OBSERVE= 2;

  void set_observe_flag() { flags|= F_OBSERVE; }
  void set_dont_observe_flag() { flags|= F_DONT_OBSERVE; }
  /**
     If F_OBSERVE is set by any plugin, then it should observe binlog
     transmission, even F_DONT_OBSERVE is set by some plugins.

     If both F_OBSERVE and F_DONT_OBSERVE are not set, then it is an old
     plugin. In this case, it should always observe binlog transmission.
   */
  bool should_observe()
  {
    return (flags & F_OBSERVE) || !(flags & F_DONT_OBSERVE);
  }
} Binlog_transmit_param;

/**
   Observe and extends the binlog dumping thread.
*/
typedef struct Binlog_transmit_observer {
  uint32 len;
  
  /**
     This callback is called when binlog dumping starts


     @param param Observer common parameter
     @param log_file Binlog file name to transmit from
     @param log_pos Binlog position to transmit from

     @retval 0 Sucess
     @retval 1 Failure
  */
  int (*transmit_start)(Binlog_transmit_param *param,
                        const char *log_file, my_off_t log_pos,
                        MYSQL_BIN_LOG *mysql_bin_log);

  /**
     This callback is called when binlog dumping stops

     @param param Observer common parameter
     
     @retval 0 Sucess
     @retval 1 Failure
  */
  int (*transmit_stop)(Binlog_transmit_param *param);

  /**
     This callback is called to reserve bytes in packet header for event transmission

     This callback is called when resetting transmit packet header to
     reserve bytes for this observer in packet header.

     The @a header buffer is allocated by the server code, and @a size
     is the size of the header buffer. Each observer can only reserve
     a maximum size of @a size in the header.

     @param param Observer common parameter
     @param header Pointer of the header buffer
     @param size Size of the header buffer
     @param len Header length reserved by this observer

     @retval 0 Sucess
     @retval 1 Failure
  */
  int (*reserve_header)(Binlog_transmit_param *param,
                        unsigned char *header,
                        unsigned long size,
                        unsigned long *len);

  /**
     This callback is called before sending an event packet to slave

     @param param Observer common parameter
     @param packet Binlog event packet to send
     @param len Length of the event packet
     @param log_file Binlog file name of the event packet to send
     @param log_pos Binlog position of the event packet to send

     @retval 0 Sucess
     @retval 1 Failure
  */
  int (*before_send_event)(Binlog_transmit_param *param,
                           unsigned char *packet, unsigned long len,
                           const char *log_file, my_off_t log_pos );

  /**
     This callback is called after an event packet is sent to the
     slave or is skipped.

     @param param             Observer common parameter
     @param event_buf         Binlog event packet buffer sent
     @param len               length of the event packet buffer
     @param skipped_log_file  Binlog file name of the event that
                              was skipped in the master. This is
                              null if the position was not skipped
     @param skipped_log_pos   Binlog position of the event that
                              was skipped in the master. 0 if not
                              skipped
     @retval 0 Sucess
     @retval 1 Failure
   */
  int (*after_send_event)(Binlog_transmit_param *param,
                          const char *event_buf, unsigned long len,
                          const char *skipped_log_file, my_off_t skipped_log_pos);

  /**
     This callback is called after resetting master status

     This is called when executing the command RESET MASTER, and is
     used to reset status variables added by observers.

     @param param Observer common parameter

     @retval 0 Sucess
     @retval 1 Failure
  */
  int (*after_reset_master)(Binlog_transmit_param *param);
} Binlog_transmit_observer;

/**
   Binlog relay IO flags
*/
enum Binlog_relay_IO_flags {
  /** Binary relay log was sync:ed */
  BINLOG_RELAY_IS_SYNCED = 1
};


/**
  Replication binlog relay IO observer parameter
*/
typedef struct Binlog_relay_IO_param {
  uint32 server_id;
  const char* host_or_ip;

  /* Master host, user and port */
  char *host;
  char *user;
  unsigned int port;

  char *master_log_name;
  my_off_t master_log_pos;

  MYSQL *mysql;                        /* the connection to master */
} Binlog_relay_IO_param;

/**
   Observes and extends the service of slave IO thread.
*/
typedef struct Binlog_relay_IO_observer {
  uint32 len;

  /**
     This callback is called when slave IO thread starts

     @param param Observer common parameter

     @retval 0 Sucess
     @retval 1 Failure
  */
  int (*thread_start)(Binlog_relay_IO_param *param);

  /**
     This callback is called when slave IO thread stops

     @param param Observer common parameter

     @retval 0 Sucess
     @retval 1 Failure
  */
  int (*thread_stop)(Binlog_relay_IO_param *param);

  /**
     This callback is called before slave requesting binlog transmission from master

     This is called before slave issuing BINLOG_DUMP command to master
     to request binlog.

     @param param Observer common parameter
     @param flags binlog dump flags

     @retval 0 Sucess
     @retval 1 Failure
  */
  int (*before_request_transmit)(Binlog_relay_IO_param *param, uint32 flags);

  /**
     This callback is called after read an event packet from master

     @param param Observer common parameter
     @param packet The event packet read from master
     @param len Length of the event packet read from master
     @param event_buf The event packet return after process
     @param event_len The length of event packet return after process

     @retval 0 Sucess
     @retval 1 Failure
  */
  int (*after_read_event)(Binlog_relay_IO_param *param,
                          const char *packet, unsigned long len,
                          const char **event_buf, unsigned long *event_len);

  /**
     This callback is called after written an event packet to relay log

     @param param Observer common parameter
     @param event_buf Event packet written to relay log
     @param event_len Length of the event packet written to relay log
     @param flags flags for relay log

     @retval 0 Sucess
     @retval 1 Failure
  */
  int (*after_queue_event)(Binlog_relay_IO_param *param,
                           const char *event_buf, unsigned long event_len,
                           uint32 flags);

  /**
     This callback is called after reset slave relay log IO status
     
     @param param Observer common parameter

     @retval 0 Sucess
     @retval 1 Failure
  */
  int (*after_reset_slave)(Binlog_relay_IO_param *param);
} Binlog_relay_IO_observer;


/**
   Register a transaction observer

   @param observer The transaction observer to register
   @param p pointer to the internal plugin structure

   @retval 0 Sucess
   @retval 1 Observer already exists
*/
int register_trans_observer(Trans_observer *observer, void *p);

/**
   Unregister a transaction observer

   @param observer The transaction observer to unregister
   @param p pointer to the internal plugin structure

   @retval 0 Sucess
   @retval 1 Observer not exists
*/
int unregister_trans_observer(Trans_observer *observer, void *p);

/**
   Register a binlog storage observer

   @param observer The binlog storage observer to register
   @param p pointer to the internal plugin structure

   @retval 0 Sucess
   @retval 1 Observer already exists
*/
int register_binlog_storage_observer(Binlog_storage_observer *observer, void *p);

/**
   Unregister a binlog storage observer

   @param observer The binlog storage observer to unregister
   @param p pointer to the internal plugin structure

   @retval 0 Sucess
   @retval 1 Observer not exists
*/
int unregister_binlog_storage_observer(Binlog_storage_observer *observer, void *p);

/**
   Register a binlog transmit observer

   @param observer The binlog transmit observer to register
   @param p pointer to the internal plugin structure

   @retval 0 Sucess
   @retval 1 Observer already exists
*/
int register_binlog_transmit_observer(Binlog_transmit_observer *observer, void *p);

/**
   Unregister a binlog transmit observer

   @param observer The binlog transmit observer to unregister
   @param p pointer to the internal plugin structure

   @retval 0 Sucess
   @retval 1 Observer not exists
*/
int unregister_binlog_transmit_observer(Binlog_transmit_observer *observer, void *p);

/**
   Register a binlog relay IO (slave IO thread) observer

   @param observer The binlog relay IO observer to register
   @param p pointer to the internal plugin structure

   @retval 0 Sucess
   @retval 1 Observer already exists
*/
int register_binlog_relay_io_observer(Binlog_relay_IO_observer *observer, void *p);

/**
   Unregister a binlog relay IO (slave IO thread) observer

   @param observer The binlog relay IO observer to unregister
   @param p pointer to the internal plugin structure

   @retval 0 Sucess
   @retval 1 Observer not exists
*/
int unregister_binlog_relay_io_observer(Binlog_relay_IO_observer *observer, void *p);

/**
   Set thread entering a condition

   This function should be called before putting a thread to wait for
   a condition. @a mutex should be held before calling this
   function. After being waken up, @f thd_exit_cond should be called.

   @param thd      The thread entering the condition, NULL means current thread
   @param cond     The condition the thread is going to wait for
   @param mutex    The mutex associated with the condition, this must be
                   held before call this function
   @param stage    The new process message for the thread
   @param old_stage The old process message for the thread
   @param src_function The caller source function name
   @param src_file The caller source file name
   @param src_line The caller source line number
*/
void thd_enter_cond(MYSQL_THD thd, mysql_cond_t *cond, mysql_mutex_t *mutex,
                    const PSI_stage_info *stage, PSI_stage_info *old_stage,
                    const char *src_function, const char *src_file,
                    int src_line);

#define THD_ENTER_COND(P1, P2, P3, P4, P5) \
  thd_enter_cond(P1, P2, P3, P4, P5, __func__, __FILE__, __LINE__)

/**
   Set thread leaving a condition

   This function should be called after a thread being waken up for a
   condition.

   @param thd      The thread entering the condition, NULL means current thread
   @param stage    The process message, ususally this should be the old process
                   message before calling @f thd_enter_cond
   @param src_function The caller source function name
   @param src_file The caller source file name
   @param src_line The caller source line number
*/
void thd_exit_cond(MYSQL_THD thd, const PSI_stage_info *stage,
                   const char *src_function, const char *src_file,
                   int src_line);

#define THD_EXIT_COND(P1, P2) \
  thd_exit_cond(P1, P2, __func__, __FILE__, __LINE__)

/**
   Get the value of user variable as an integer.

   This function will return the value of variable @a name as an
   integer. If the original value of the variable is not an integer,
   the value will be converted into an integer.

   @param name     user variable name
   @param value    pointer to return the value
   @param null_value if not NULL, the function will set it to true if
   the value of variable is null, set to false if not

   @retval 0 Success
   @retval 1 Variable not found
*/
int get_user_var_int(const char *name,
                     long long int *value, int *null_value);

/**
   Get the value of user variable as a double precision float number.

   This function will return the value of variable @a name as real
   number. If the original value of the variable is not a real number,
   the value will be converted into a real number.

   @param name     user variable name
   @param value    pointer to return the value
   @param null_value if not NULL, the function will set it to true if
   the value of variable is null, set to false if not

   @retval 0 Success
   @retval 1 Variable not found
*/
int get_user_var_real(const char *name,
                      double *value, int *null_value);

/**
   Get the value of user variable as a string.

   This function will return the value of variable @a name as
   string. If the original value of the variable is not a string,
   the value will be converted into a string.

   @param name     user variable name
   @param value    pointer to the value buffer
   @param len      length of the value buffer
   @param precision precision of the value if it is a float number
   @param null_value if not NULL, the function will set it to true if
   the value of variable is null, set to false if not

   @retval 0 Success
   @retval 1 Variable not found
*/
int get_user_var_str(const char *name,
                     char *value, unsigned long len,
                     unsigned int precision, int *null_value);

  

#ifdef __cplusplus
}
#endif
#endif /* REPLICATION_H */
