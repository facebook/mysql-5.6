#ifndef RPL_SHARDBEATS_H_INCLUDED
#define RPL_SHARDBEATS_H_INCLUDED

#include <atomic>
#include <boost/circular_buffer.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "mysql/psi/mysql_cond.h"
#include "mysql/psi/mysql_mutex.h"

class THD;

// GLOBAL Variables
// the table that should be present on all shards
// where shardbeats are injected.
extern char *shardbeat_table;
// the user as which to add shardbeats
extern char *shardbeat_user;
// interval between shardbeats on silent shards
extern uint shardbeat_interval_ms;

/**
 * This class contains the functionality for Shardbeats.
 * Shardbeats inject simple INSERTs/heartbeats for each user facing database.
 * There is a designated heartbeat/shardbeat table in each database/shard,
 * where we can insert an integer. Ideally this table will have BLACKHOLE
 * Engine. Shardbeats will get replicated to the secondary and will help
 * downstream tailers distinguish silent shards from failure situations.
 * These shardbeats are injected on a global SLA that is guaranteed
 * by the MYSQL service in agreement with downstream services.
 *
 * With the expectation that a shardbeat will be injected every
 * SLA milliseconds, the absence of writes on the tailer side
 * will lead to this logic.
 * 1. no writes at all on a shard - Transaction problem or
 *    READ ONLY SHARD
 * 2. no writes at all on any shard - Replication broken or
 *    mysql is unhealthy
 * 3. Shardbeats but no user writes - Silent Shard
 */
class Shardbeats_manager {
 public:
  /**
   * static accessor for singleton which will return
   * nullptr if shardbeats is not enabled yet.
   */
  static Shardbeats_manager *get();

  /**
   * static accessor for singleton which will never return
   * nullptr.
   */
  static Shardbeats_manager *get_or_create();

  static void destroy();

  ~Shardbeats_manager();

  enum class State { STOPPED, STARTING, RUNNING, STOPPING };

  // A simple struct to keep range of time
  // that a mysql error consecutively happened
  // on a db/shard
  class Error_details {
   public:
    uint mysql_errno;
    unsigned long long error_time_start;
    unsigned long long error_time_last;

    // Return a diagnostic string for this error
    std::string to_string() const;
  };

  class Shard_stats {
   public:
    static const size_t HISTORY_LENGTH = 5;
    Shard_stats();
    std::string db;

    // Some commonly expected error codes.
    // ER_OPTION_PREVENTS_STATEMENT (Read only)
    // ER_NO_SUCH_TABLE (shardbeater table is missing)
    // ER_LOCK_WAIT_TIMEOUT (during OLM)
    // ER_DB_READ_ONLY (during cutover phase of OLM)
    // maintain the last few errors
    boost::circular_buffer<Error_details> last_errors;

    // Preserve timestamps of last few successful heartbeats
    boost::circular_buffer<unsigned long long> last_hbs;

    // total successful shardbeats since startup
    unsigned long long num_shardbeats;

    // total failed shardbeats since startup
    unsigned long long num_failures;

    // Return a diagnostic string for the last_hbs array
    std::string last_hbs_as_str() const;

    // Return a diagnostic string for the last_errors array
    std::string last_errors_as_str() const;
  };

  // Get a copy of the db_trx_map.
  // Protected with mysql_bin_log.LOCK_log
  void get_db_trx_times(
      std::unordered_map<std::string, unsigned long long> *db_trx_map);

  /**
   * Update the last transaction time for this database.
   * If the Database has a trx in ordered commit, it is not silent
   * for that time + SLA
   */
  void update_db_trx_times(THD *thd, unsigned long long oc_flush_timestamp);

  // Stop the Thread for Shardbeat injection
  bool start_shardbeater_thread(THD *thd);

  // Start the Thread for Shardbeat injection
  bool stop_shardbeater_thread(THD *thd);

  // Show status of the shardbeater
  bool show_status_cmd(THD *thd);

  // Used by the shardbeater thread to update run state.
  void transition_to_state(State s);

  // Is the state of shardbeater = SHARDBEATER_RUNNING
  bool is_running();

  // Actual function which is run inside the shardbeats thread.
  void execute();

  // format comment on update of global variable.
  void expand_format_comment();

  // create latest list of blocked dbs on update of global variable.
  void create_blocked_dbs();

 private:
  // Private constructor
  Shardbeats_manager();

  // Once INSERT of sheardbeat has finished,
  // maintain stats for success/failure
  void post_write(const std::string &db, unsigned long long current_ts);

  void cleanup_thread();

  void maintain_ro_stats(bool ro, unsigned long long current_ts_ms);

  void maintain_is_slave_stats(bool isslave, unsigned long long current_ts_ms);

  // Initialize the mutexes
  void init_mutexes();

  // Destroy the mutexes
  void cleanup_mutexes();

  typedef std::pair<std::string, std::string> shard_rs_t;
  // The core function for checking and inserting
  // shardbeat into a silent shard
  void check_and_insert_shardbeat(
      const std::pair<std::string, shard_rs_t> &dbinfo, unsigned long long SLA);

 private:
  // The thd of the shardbeater thread
  THD *sb_thd = nullptr;

  // shard -> shardbeat_stats for reporting
  std::unordered_map<std::string, Shard_stats> db_sb_stats;

  // The last time in microsecs that a trx for this db
  // went into ordered commit. protected by mysql_bin_log.LOCK_log
  std::unordered_map<std::string, unsigned long long> m_last_trx_time;

  // conditional variable which is used to wait
  // for running state changes
  mysql_cond_t COND_shardbeater_run_cond;

  // mutex which protects all key datastructures,
  // e.g. state and db_sb_stats
  mysql_mutex_t LOCK_shardbeater;

  // Running state of the shardbeater
  State state = State::STOPPED;

  // list of blocked dbs
  std::unordered_set<std::string> skip_databases;

  // blocked dbs list created for each iter.
  std::unordered_set<std::string> skip_databases_iter;

  // formatted string for query comment
  std::string format_str_comment;

  // formatted string for each iter
  std::string format_str_comment_iter;

  // read only failure logging helpers
  uint64_t ro_failure_count = 0;
  std::atomic<unsigned long long> ro_ON_ts;
  std::atomic<unsigned long long> ro_OFF_ts;
  std::atomic<bool> current_ro_state;

  // read only failure logging helpers
  uint64_t is_slave_failure_count = 0;
  std::atomic<unsigned long long> is_slave_ON_ts;
  std::atomic<unsigned long long> is_slave_OFF_ts;
  std::atomic<bool> current_is_slave_state;

  // convenience member to store the
  // database -> last trx time during each iteration of
  // shardbeats loop.
  std::unordered_map<std::string, unsigned long long> db_last_trx_time;

  // To maintain the insertion id of shardbeat for each db.
  std::unordered_map<std::string, int64_t> shardbeat_count;

  // convenience member to store db -> shard loop for each
  // iteration of shardbeats loop
  std::vector<std::pair<std::string, shard_rs_t>> shards;

  // the local ipv6 address which is provided as a comment
  // in trx.
  std::string ipv6_addr;
};

// extern Shardbeats_manager rpl_shardbeats;

#endif
