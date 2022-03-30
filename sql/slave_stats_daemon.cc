#include "my_global.h"
#include "mysql.h"
#include "mysql_com.h"
#include "sql_base.h"
#include "sql_show.h"
#include "sql_string.h"
#include "global_threads.h"
#include "srv_session.h"
#include "my_time.h"
#include "rpl_slave.h"
#include "rpl_mi.h"
#include "slave_stats_daemon.h"

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
// Function removed after OpenSSL 1.1.0
#define ERR_remove_state(x)
#endif

/*
 * The Slave stats daemon thread is responsible for
 * continuously sending lag statistics from slaves to masters
 */

pthread_t slave_stats_daemon_thread;
mysql_mutex_t LOCK_slave_stats_daemon;
mysql_cond_t COND_slave_stats_daemon;

/* connection/read timeout in seconds*/
const int REPLICA_STATS_NET_TIMEOUT = 5;

#ifdef HAVE_REPLICATION
static bool abort_slave_stats_daemon;

static bool connected_to_master = false;

/**
  Create and initialize the mysql object, and connect to the
  master.

  @retval true if connection successful
  @retval false otherwise.
*/
static int safe_connect_slave_stats_thread_to_master(MYSQL * &mysql)
{
  if (mysql != NULL) {
    mysql_close(mysql);
  }
  mysql = mysql_init(NULL);
  if (!mysql) {
    return false;
  }
  mysql_options(mysql, MYSQL_OPT_CONNECT_TIMEOUT,
    (char *) &REPLICA_STATS_NET_TIMEOUT);
  mysql_options(mysql, MYSQL_OPT_READ_TIMEOUT,
    (char *) &REPLICA_STATS_NET_TIMEOUT);
  configure_master_connection_options(mysql, active_mi);

  char pass[MAX_PASSWORD_LENGTH + 1];
  int password_size = sizeof(pass);
  if (active_mi->get_password(pass, &password_size)) {
    return false;
  }
  DBUG_EXECUTE_IF("dbug.replica_stats_force_disconnet_primary",
                  {return false;});
  if (!mysql_real_connect(mysql, active_mi->host, active_mi->get_user(), pass,
                          0, active_mi->port, 0, 0)) {
    return false;
  }
  return true;
}

pthread_handler_t handle_slave_stats_daemon(void *arg MY_ATTRIBUTE((unused)))
{
  THD *thd= NULL;
  int error = 0;
  struct timespec abstime;

  DBUG_ENTER("handle_slave_stats_daemon");

  pthread_detach_this_thread();

  slave_stats_daemon_thread = pthread_self();

  MYSQL *mysql = nullptr;
  while(true) {
    mysql_mutex_lock(&LOCK_slave_stats_daemon);
    set_timespec(abstime, write_stats_frequency);
    while ((!error || error == EINTR) && !abort_slave_stats_daemon) {
      /*
       write_stats_frequency is set to 0. Do not send stats to master.
       Wait until a signal is received either for aborting the thread or for
       updating write_stats_frequency.
      */
      if (write_stats_frequency == 0) {
        error = mysql_cond_wait(&COND_slave_stats_daemon, &LOCK_slave_stats_daemon);
      } else {
        /*
        wait for write_stats_frequency seconds before sending next set
        of slave lag statistics
        */
        error = mysql_cond_timedwait(&COND_slave_stats_daemon, &LOCK_slave_stats_daemon, &abstime);
      }
    }

    mysql_mutex_unlock(&LOCK_slave_stats_daemon);

    if (abort_slave_stats_daemon)
      break;

    if (enable_raft_plugin && !raft_send_replica_statistics) {
      error = 0;
      continue;
    }

    if (error == ETIMEDOUT) {
      // Initialize connection thd, if not already done.
      if (thd == NULL) {
        my_thread_init();
        thd= new THD;
        THD_CHECK_SENTRY(thd);
        thd->thread_stack= (char*) &thd;
        my_net_init(thd->get_net(), 0);
        thd->store_globals();
      }

      DBUG_EXECUTE_IF("dbug.replica_stats_force_disconnet_primary",
                  {connected_to_master = false;});
      // If not connected to current master, try connection. If not
      // successful, try again in next cycle
      if (!connected_to_master) {
        connected_to_master = safe_connect_slave_stats_thread_to_master(mysql);
        if (connected_to_master) {
          DBUG_PRINT("info",
              ("Slave Stats Daemon: connected to master '%s@%s:%d'",
              active_mi->get_user(), active_mi->host, active_mi->port));
        } else {
          DBUG_PRINT("info",
              ("Slave Stats Daemon: Couldn't connect to master '%s@%s:%d', "
              "will try again during next cycle, (Error: %s)",
              active_mi->get_user(), active_mi->host, active_mi->port, mysql_error(mysql)));
        }
      }
      if (connected_to_master &&
          (enable_raft_plugin ||
           active_mi->slave_running == MYSQL_SLAVE_RUN_CONNECT)) {
        ulong seconds_since_start = my_time(0) - server_start_time;
        std::pair<longlong, longlong> time_lag_behind_master = get_time_lag_behind_master(active_mi);
        int milli_sec_behind_master = std::max((int)time_lag_behind_master.second, 0);
        DBUG_EXECUTE_IF("dbug.force_high_lag_behind_master", { milli_sec_behind_master = 9999; });
        if (seconds_since_start > write_send_replica_statistics_wait_time_seconds ||
            (ulong)milli_sec_behind_master < write_stop_throttle_lag_milliseconds) {
          if (send_replica_statistics_to_master(mysql, active_mi, milli_sec_behind_master)) {
            DBUG_PRINT("info", ("Slave Stats Daemon: Failed to send lag "
                              "statistics, resetting connection, (Error: %s)",
                              mysql_error(mysql)));
            connected_to_master = false;
          }
        }
      }
      error = 0;
    }
  }
  mysql_close(mysql);
  mysql = nullptr;
  connected_to_master = false;
  if (thd != NULL) {
    net_end(thd->get_net());
    thd->release_resources();
    delete (thd);
  }
  // DBUG_LEAVE; // Can't use DBUG_RETURN after my_thread_end
  my_thread_end();
  ERR_remove_state(0);
  DBUG_ASSERT(slave_stats_daemon_thread_counter > 0);
  slave_stats_daemon_thread_counter--;
  pthread_exit(0);
  return (NULL);
}

/* Start handle Slave Stats Daemon thread */
bool start_handle_slave_stats_daemon() {
  DBUG_ENTER("start_handle_slave_stats_daemon");
  pthread_t hThread;
  int error;
  slave_stats_daemon_thread_counter++;
  error = mysql_thread_create(key_thread_handle_slave_stats_daemon,
                                    &hThread, &connection_attrib,
                                    handle_slave_stats_daemon, 0);
  if (error) {
    sql_print_warning(
        "Can't create Slave Stats Daemon thread (errno= %d)", error);
    DBUG_ASSERT(slave_stats_daemon_thread_counter > 0);
    slave_stats_daemon_thread_counter--;
    DBUG_RETURN(false);
  }
  sql_print_information("Successfully created Slave Stats Daemon thread: 0x%lx",
              (ulong)slave_stats_daemon_thread);
  DBUG_RETURN(true);
}

/* Initiate shutdown of handle Slave Stats Daemon thread */
void stop_handle_slave_stats_daemon() {
  DBUG_ENTER("stop_handle_slave_stats_daemon");
  abort_slave_stats_daemon = true;
  mysql_mutex_lock(&LOCK_slave_stats_daemon);
  sql_print_information("Shutting down Slave Stats Daemon thread: 0x%lx",
              (ulong)slave_stats_daemon_thread);
  mysql_cond_signal(&COND_slave_stats_daemon);
  mysql_mutex_unlock(&LOCK_slave_stats_daemon);
  while(slave_stats_daemon_thread_counter > 0) {
    // wait for the thread to finish, sleep for 10ms
    my_sleep(10000);
  }
  // Reset abort_slave_stats_daemon so slave_stats_daemon can be spawned in future
  abort_slave_stats_daemon = false;
  DBUG_VOID_RETURN;
}

#endif
