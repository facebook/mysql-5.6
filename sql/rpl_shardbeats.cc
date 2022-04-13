#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <algorithm>
#include <boost/algorithm/string.hpp>

#include "sql/rpl_shardbeats.h"

#include "include/mutex_lock.h"
#include "mysql/com_data.h"

#include "sql/auth/auth_acls.h"
#include "sql/binlog.h"
#include "sql/dd/cache/dictionary_client.h"  // dd::cache::Dictionary_client
#include "sql/log.h"
#include "sql/mysqld.h"
#include "sql/mysqld_thd_manager.h"
#include "sql/protocol_classic.h"
#include "sql/sql_class.h"
#include "sql/sql_lex.h"
#include "sql/sql_parse.h"
#include "sql/sql_profile.h"
#include "sql/sys_vars.h"

#include <utility>

#ifdef HAVE_PSI_INTERFACE
static PSI_cond_key key_COND_shardbeater_run_cond;
static PSI_mutex_key key_LOCK_shardbeater;
#endif /* HAVE_PSI_INTERFACE */

// expected table for sharbeats in each database
char *shardbeat_table = nullptr;
// ACL user to be used to inject shardbeats
char *shardbeat_user = nullptr;
// By default inject a shardbeat every minute
uint shardbeat_interval_ms = 60000;
// query comment to prefix the insert.
char *shardbeat_query_comment_format = nullptr;
// list of extra blocked dbs.
char *shardbeat_blocked_dbs = nullptr;
// verbosity level of logging
uint shardbeat_vlog_level = 0;
// enable the shardbeater
bool enable_shardbeater = true;

static Sys_var_uint Sys_shardbeat_interval_ms(
    "shardbeat_interval_ms",
    "Interval in milliseconds in which shardbeats are injected on "
    "silent databases",
    GLOBAL_VAR(shardbeat_interval_ms), CMD_LINE(OPT_ARG),
    VALID_RANGE(0, UINT_MAX), DEFAULT(60000), BLOCK_SIZE(1), NO_MUTEX_GUARD,
    NOT_IN_BINLOG, ON_CHECK(NULL), ON_UPDATE(NULL));

static Sys_var_charptr Sys_shardbeat_table(
    "shardbeat_table", "Name of table in which to insert a shardbeat",
    GLOBAL_VAR(shardbeat_table), CMD_LINE(OPT_ARG), IN_FS_CHARSET,
    DEFAULT("blackhole"), NO_MUTEX_GUARD, NOT_IN_BINLOG);

static Sys_var_charptr Sys_shardbeat_user(
    "shardbeat_user", "Name of user as which to insert regular shardbeats",
    GLOBAL_VAR(shardbeat_user), CMD_LINE(OPT_ARG), IN_FS_CHARSET, DEFAULT(""),
    NO_MUTEX_GUARD, NOT_IN_BINLOG);

static bool shardbeat_comment_str_update(sys_var *, THD *, enum_var_type) {
  Shardbeats_manager::get_or_create()->expand_format_comment();
  return false;
}

static Sys_var_charptr Sys_shardbeat_insert_comment_fs(
    "shardbeat_query_comment_format",
    "Formatted string to be used for shardbeats insert",
    GLOBAL_VAR(shardbeat_query_comment_format), CMD_LINE(OPT_ARG),
    IN_FS_CHARSET, DEFAULT(""), NO_MUTEX_GUARD, NOT_IN_BINLOG, ON_CHECK(NULL),
    ON_UPDATE(shardbeat_comment_str_update));

static bool shardbeat_blocked_dbs_update(sys_var *, THD *, enum_var_type) {
  Shardbeats_manager::get_or_create()->create_blocked_dbs();
  return false;
}

static Sys_var_charptr Sys_shardbeat_blocked_dbs(
    "shardbeat_blocked_dbs",
    "List of comma separated database names on which not to insert shardbeats",
    GLOBAL_VAR(shardbeat_blocked_dbs), CMD_LINE(OPT_ARG), IN_FS_CHARSET,
    DEFAULT(""), NO_MUTEX_GUARD, NOT_IN_BINLOG, ON_CHECK(NULL),
    ON_UPDATE(shardbeat_blocked_dbs_update));

static Sys_var_uint Sys_shardbeat_vlog_level(
    "shardbeat_vlog_level",
    "Verbosity level of logging into mysqld error log for shardbeater",
    GLOBAL_VAR(shardbeat_vlog_level), CMD_LINE(OPT_ARG), VALID_RANGE(0, 10),
    DEFAULT(0), BLOCK_SIZE(1), NO_MUTEX_GUARD, NOT_IN_BINLOG, ON_CHECK(NULL),
    ON_UPDATE(NULL));

static Sys_var_bool Sys_enable_shardbeater("enable_shardbeater",
                                           "Enables Shardbeater",
                                           GLOBAL_VAR(enable_shardbeater),
                                           CMD_LINE(OPT_ARG), DEFAULT(true),
                                           NO_MUTEX_GUARD, NOT_IN_BINLOG,
                                           ON_CHECK(NULL), ON_UPDATE(NULL));

// The global singleton for shardbeat interactions
std::unique_ptr<Shardbeats_manager> shardbeats_mngr;

namespace {

const int INFREQUENT_LOGGING = 200131;

typedef std::pair<std::string, std::string> shard_rs_t;

// Get mysql databases and their shard and replicaset metadata
// Skip all databases for which Shardbeats should not be generated
// Skip databases without trx metadata
// Skip databases which are "olm" : "1"
// return 1 on failure, 0 on success
int get_dbs_with_filter(THD *thd,
                        const std::unordered_set<std::string> &skip_databases,
                        std::vector<std::pair<std::string, shard_rs_t>> *dbs) {
  dd::cache::Dictionary_client::Auto_releaser releaser(thd->dd_client());

  std::vector<const dd::Schema *> schema_vector;
  if (thd->dd_client()->fetch_global_components(&schema_vector)) {
    return 1;
  }

  for (const dd::Schema *schema : schema_vector) {
    std::string dbname = schema->name().c_str();

    // skip BLOCKED_DBS
    if (skip_databases.find(dbname) != skip_databases.end()) {
      continue;
    }
    std::string db_metadata = schema->get_db_metadata().c_str();

    // convert the db metadata into the shard and replicaset ids.
    shard_rs_t tmp_info;
    bool olm = false;
    if (db_metadata.empty() ||
        THD::get_shard_rs_id(db_metadata, &tmp_info, &olm) || olm) {
      // error in parsing db_metadata
      static int error_msg_count = 0;
      if ((error_msg_count++ % INFREQUENT_LOGGING == 0 &&
           shardbeat_vlog_level >= 2) ||
          shardbeat_vlog_level >= 4) {
        // NO_LINT_DEBUG
        sql_print_error(
            "shardbeater db_metadata absent/parsing error/ongoing olm for db: "
            "%s "
            "metadata:'%s' olm:%d",
            dbname.c_str(), db_metadata.c_str(), olm);
      }
      continue;
    }

    dbs->push_back(std::make_pair(std::move(dbname), tmp_info));
  }

  return 0;
}

const char *EXPECTED_INTF_NAME = "eth0";

// Returns a best effort ipv6 address for the local host.
// to be used in query comments for external systems like
// wormhole tailers. Multiple ipv6 addresses can be present
// for same interface e.g. eth0 and this code uses a one time
// getnameinfo/(lookup into /etc/hosts) to determine the
// ipv6 addresses that is the best to be used.
std::string get_ipv6addr() {
  struct ifaddrs *ifap = nullptr;
  struct ifaddrs *ifa = nullptr;
  int retc = getifaddrs(&ifap);
  if (retc != 0) {
    // NO_LINT_DEBUG
    sql_print_error("shardbeater getifaddrs call returned error %d", retc);
    return "";
  }

  std::string ret_value;
  for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
    // check it is IPV6
    if (ifa->ifa_addr->sa_family != AF_INET6) {
      continue;
    }

    char addressBuffer[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr,
              addressBuffer, INET6_ADDRSTRLEN);

    // HACK_ALERT
    // We only look for eth0 interface now.
    std::string intf_name(ifa->ifa_name);
    if (intf_name != EXPECTED_INTF_NAME) {
      continue;
    }

    // Filter out ipv6 addresses that are not global
    std::string value(addressBuffer);
    size_t colon_count = std::count(value.begin(), value.end(), ':');
    if (colon_count != 7) {
      continue;
    }

    char hbuf[NI_MAXHOST];
    if (getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in6), hbuf,
                    sizeof(hbuf), NULL, 0, NI_NAMEREQD))
      continue;

    // NO_LINT_DEBUG
    sql_print_information("hostname found = %s ipv6 addr = %s", hbuf,
                          addressBuffer);
    ret_value = std::move(value);
    break;
  }

  freeifaddrs(ifap);
  return ret_value;
}

const char *STD_BLOCKED_DBS[] = {"mysql", "sys", "information_schema",
                                 "performance_schema", "mtr"};
}  // namespace

// The main function of the shardbeater thread
extern "C" void *generate_shardbeats(void *shardbeat_singleton) {
  // Setup this thread
  // borrowed from One_thread_connection_handler::add_connection
  my_thread_init();

  // NO_LINT_DEBUG
  sql_print_information("Shard level Heartbeater thread started");

  Shardbeats_manager *smgr = (Shardbeats_manager *)shardbeat_singleton;
  assert(smgr != nullptr);
  // Call the actual execute function.
  smgr->execute();

  // NO_LINT_DEBUG
  sql_print_information("Shard level Heartbeater thread finished");

  my_thread_end();
  pthread_exit(0);
  return 0;
}

Shardbeats_manager *Shardbeats_manager::get() { return shardbeats_mngr.get(); }

Shardbeats_manager *Shardbeats_manager::get_or_create() {
  if (!shardbeats_mngr) {
    shardbeats_mngr.reset(new Shardbeats_manager);
  }
  return shardbeats_mngr.get();
}

void Shardbeats_manager::destroy() { shardbeats_mngr.reset(); }

Shardbeats_manager::Shardbeats_manager()
    : sb_thd(nullptr),
      ro_ON_ts(0),
      ro_OFF_ts(0),
      current_ro_state(true),
      is_slave_ON_ts(0),
      is_slave_OFF_ts(0),
      current_is_slave_state(false) {
  // initialize the mutexes
  init_mutexes();

  // Save ipv6 address one time for WH comment.
  ipv6_addr = get_ipv6addr();

  create_blocked_dbs();

  expand_format_comment();
}

Shardbeats_manager::~Shardbeats_manager() { cleanup_mutexes(); }

void Shardbeats_manager::expand_format_comment() {
  std::string fs;
  if (shardbeat_query_comment_format &&
      !fs.assign(shardbeat_query_comment_format).empty()) {
    boost::replace_all(fs, "{ipaddr}", ipv6_addr);
  }

  mysql_mutex_lock(&LOCK_shardbeater);
  format_str_comment.swap(fs);
  mysql_mutex_unlock(&LOCK_shardbeater);
}

void Shardbeats_manager::create_blocked_dbs() {
  std::unordered_set<std::string> skip_databases_tmp;
  size_t num_elems = sizeof(STD_BLOCKED_DBS) / sizeof(const char *);
  for (size_t i = 0; i < num_elems; i++) {
    skip_databases_tmp.insert(STD_BLOCKED_DBS[i]);
  }
  skip_databases_iter = skip_databases;
  std::string bdbstr;
  if (shardbeat_blocked_dbs && !bdbstr.assign(shardbeat_blocked_dbs).empty()) {
    // assume spaces and other white spaces after the commas
    std::vector<std::string> db_names;
    boost::split(db_names, bdbstr, boost::is_any_of("\t ,"),
                 boost::token_compress_on);
    for (const std::string &db : db_names) {
      skip_databases_tmp.insert(db);
    }
  }

  mysql_mutex_lock(&LOCK_shardbeater);
  skip_databases.swap(skip_databases_tmp);
  mysql_mutex_unlock(&LOCK_shardbeater);
}

void Shardbeats_manager::maintain_ro_stats(bool ro,
                                           unsigned long long current_ts_ms) {
  if (current_ro_state != ro) {
    if (ro) {
      // NO_LINT_DEBUG
      sql_print_information(
          "shardbeater: read_only detected as ON. read_only was OFF from "
          "%llu(ms) "
          "to "
          "%llu(ms)",
          ro_OFF_ts.load(), current_ts_ms);
      ro_ON_ts = current_ts_ms;
    } else {
      // NO_LINT_DEBUG
      sql_print_information(
          "shardbeater: read_only detected as OFF. read_only was ON from "
          "%llu(ms) "
          "to "
          "%llu(ms)",
          ro_ON_ts.load(), current_ts_ms);
      ro_OFF_ts = current_ts_ms;
    }
    current_ro_state = ro;
    ro_failure_count = 0;
  }

  if (ro) {
    ro_failure_count++;
  }
}

void Shardbeats_manager::maintain_is_slave_stats(
    bool isslave, unsigned long long current_ts_ms) {
  if (current_is_slave_state != isslave) {
    if (isslave) {
      // NO_LINT_DEBUG
      sql_print_information(
          "shardbeater: is_replica detected as ON. is_replica was OFF from "
          "%llu(ms) "
          "to "
          "%llu(ms)",
          is_slave_OFF_ts.load(), current_ts_ms);
      is_slave_ON_ts = current_ts_ms;
    } else {
      // NO_LINT_DEBUG
      sql_print_information(
          "shardbeater: is_replica detected as OFF. is_replica was ON from "
          "%llu(ms) "
          "to "
          "%llu(ms)",
          is_slave_ON_ts.load(), current_ts_ms);
      is_slave_OFF_ts = current_ts_ms;
    }
    current_is_slave_state = isslave;
    is_slave_failure_count = 0;
  }

  if (isslave) {
    is_slave_failure_count++;
  }
}

void Shardbeats_manager::check_and_insert_shardbeat(
    const std::pair<std::string, shard_rs_t> &dbinfo, unsigned long long SLA) {
  std::string table_name(shardbeat_table);
  // table name should be valid. Paranoid check as table name
  // could have flipped.
  if (table_name.empty()) {
    return;
  }

  const std::string &db(dbinfo.first);
  if (skip_databases_iter.find(db) != skip_databases_iter.end()) {
    return;
  }

  const std::string &shard_id(dbinfo.second.first);
  const std::string &rs_id(dbinfo.second.second);
  if (shard_id.empty() || rs_id.empty()) {
    // skip databases with empty db_metadata
    static int error_msg_count = 0;
    if ((error_msg_count++ % INFREQUENT_LOGGING == 0 &&
         shardbeat_vlog_level >= 2) ||
        shardbeat_vlog_level >= 4) {
      // NO_LINT_DEBUG
      sql_print_information(
          "shardbeater db_metadata empty for db. Will skip: db:'%s' shard:'%s' "
          "rs:'%s'",
          db.c_str(), shard_id.c_str(), rs_id.c_str());
    }
    return;
  }

  unsigned long long current_ts = my_micro_time();

  const auto itr = db_last_trx_time.find(db);
  bool skip = false;
  if (itr != db_last_trx_time.end()) {
    unsigned long long last_trx_ts = itr->second;
    // last trx is in the past and a SLA period has passed.
    skip = ((last_trx_ts + SLA) > current_ts);
    if (skip) {
      if (shardbeat_vlog_level >= 3) {
        // NO_LINT_DEBUG
        sql_print_information(
            "Skipping db %s due to within SLA: %llu(us) %llu(us)", db.c_str(),
            current_ts, last_trx_ts);
      }
      // insert heartbeats for silent shards only
      return;
    }
  }

  int64_t &sb_num = shardbeat_count[db];
  sb_num++;

  int64_t sb_val = (int64_t)current_ts / 1000000;

  // borrowed from do_command
  sb_thd->lex->set_current_query_block(nullptr);
  sb_thd->clear_error();  // Clear error message
  sb_thd->get_stmt_da()->reset_diagnostics_area();

  // One typical comment format can be.
  // shardbeat_query_comment_format =
  // WH:1 #.T:sb #.S:{shard} #.I:{ipaddr} #rs:{replicaset}
  char s_buf[300];

  std::string comment;
  if (!format_str_comment_iter.empty()) {
    comment = format_str_comment_iter;
    boost::replace_all(comment, "{shard}", shard_id);
    boost::replace_all(comment, "{replicaset}", rs_id);
    sprintf(s_buf, "USE %s; /* %s */ INSERT INTO %s VALUES(%ld);", db.c_str(),
            comment.c_str(), table_name.c_str(), sb_val);
  } else {
    sprintf(s_buf, "INSERT INTO %s.%s VALUES(%ld);", db.c_str(),
            table_name.c_str(), sb_val);
  }
  size_t len = strlen(s_buf);
  char *buf = sb_thd->strmake(s_buf, len);

  if (shardbeat_vlog_level >= 3) {
    // NO_LINT_DEBUG
    sql_print_information("Inserting heartbeat %s %s sb_num:%ld sb_val:%ld %s",
                          db.c_str(), table_name.c_str(), sb_num, sb_val,
                          s_buf);
  }

#if defined(ENABLED_PROFILING)
  sb_thd->profiling->start_new_query();
  sb_thd->profiling->set_query_source(buf, len);
#endif

  Protocol_classic *protocol = sb_thd->get_protocol_classic();
  COM_DATA com_data;
  protocol->create_command(&com_data, COM_QUERY, (uchar *)buf, len);

  auto start_time = std::chrono::steady_clock::now();

  dispatch_command(sb_thd, &com_data, COM_QUERY);

  uint64_t total_duration_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start_time)
          .count();

  post_write(db, current_ts);

#if defined(ENABLED_PROFILING)
  sb_thd->profiling->finish_current_query();
#endif

  if (sb_thd->is_error()) {
    if (shardbeat_vlog_level >= 2) {
      // NO_LINT_DEBUG
      sql_print_error(
          "Error hit while inserting shardbeat: %s errcode: %u %s "
          "duration_ms: %u sb_num:%ld sb_val:%ld",
          db.c_str(), sb_thd->get_stmt_da()->mysql_errno(),
          sb_thd->get_stmt_da()->message_text(), total_duration_ms, sb_num,
          sb_val);
    }
  }
}

void Shardbeats_manager::execute() {
  sb_thd = new THD;
  sb_thd->set_new_thread_id();
  sb_thd->thread_stack = (char *)&sb_thd;
  sb_thd->store_globals();
  pthread_setname_np(pthread_self(), "shardbeats");

  mysql_thread_set_psi_id(sb_thd->thread_id());

  // NO_LINT_DEBUG
  sql_print_information("shardbeater thread id: %lu",
                        sb_thd->system_thread_id());

  Global_THD_manager *thd_manager = Global_THD_manager::get_instance();
  thd_manager->add_thd(sb_thd);

  // update the run state
  transition_to_state(State::RUNNING);

  Security_context *sctx = sb_thd->security_context();
  if (acl_getroot(sb_thd, sctx, shardbeat_user, "localhost", "::1", nullptr)) {
    // NO_LINT_DEBUG
    sql_print_error(
        "Failed to get ACL for shardbeat_user. Exit shardbeat thread");
    cleanup_thread();
    return;
  }

  bool is_super = sctx->check_access(SUPER_ACL);
  // NO_LINT_DEBUG
  sql_print_information("Shardbeater user %s was resolved has super_priv: %d",
                        shardbeat_user, is_super);

  // borrowed from thd_prepare_connection
  lex_start(sb_thd);

  Protocol_classic *protocol = sb_thd->get_protocol_classic();
  protocol->add_client_capability(CLIENT_MULTI_QUERIES);
  protocol->set_vio(nullptr);
  int64_t loop_cnt = 0;

  unsigned long long SLA_iter = shardbeat_interval_ms * 1000;
  // Allow 1/10th of the SLA to be waited for locks per db.
  // We are assuming that DDL like RENAME table will likely only
  // happen for 1 database at a given time.
  sb_thd->variables.lock_wait_timeout_nsec = (SLA_iter * 100);
  sb_thd->variables.high_priority_lock_wait_timeout_nsec = (SLA_iter * 100);
  // NO_LINT_DEBUG
  sql_print_information("shardbeats SLA is %u milliseconds",
                        shardbeat_interval_ms);

  while (!sb_thd->killed && is_running()) {
    // in microseconds.
    unsigned long long SLA = shardbeat_interval_ms * 1000;

    if (SLA != SLA_iter) {
      sb_thd->variables.lock_wait_timeout_nsec = (SLA * 100);
      sb_thd->variables.high_priority_lock_wait_timeout_nsec = (SLA * 100);
      // NO_LINT_DEBUG
      sql_print_information("shardbeats SLA is %u milliseconds",
                            shardbeat_interval_ms);
      SLA_iter = SLA;
    }

    // Each loop iteration sleeps for half-life of SLA
    usleep(SLA / 2);
    loop_cnt++;

    if (!enable_shardbeater) {
      static int error_msg_count = 0;
      if (error_msg_count++ % INFREQUENT_LOGGING == 0 ||
          shardbeat_vlog_level >= 3) {
        // NO_LINT_DEBUG
        sql_print_information(
            "shardbeater is disabled. Skipping sharbeats insertion");
      }
      continue;
    }

    std::string table_name(shardbeat_table);
    // table name should be valid.
    if (table_name.empty()) {
      static int error_msg_count = 0;
      if ((error_msg_count++ % INFREQUENT_LOGGING == 0 &&
           shardbeat_vlog_level >= 1) ||
          shardbeat_vlog_level >= 3) {
        // NO_LINT_DEBUG
        sql_print_information(
            "shardbeat_table is empty. Skipping sharbeats insertion");
      }
      continue;
    }

    // Early check on read_only. This injects the shardbeat only
    // on the primary and filters out all the secondaries
    bool ro_fail = check_readonly(sb_thd, false);
    unsigned long long current_ts = my_micro_time();
    maintain_ro_stats(ro_fail, current_ts / 1000 /* milliseconds */);
    if (ro_fail) {
      static int error_msg_count = 0;
      if ((error_msg_count++ % INFREQUENT_LOGGING == 0 &&
           shardbeat_vlog_level >= 1) ||
          shardbeat_vlog_level >= 3) {
        // NO_LINT_DEBUG
        sql_print_information("read_only is set. Skipping sharbeats insertion");
      }
      continue;
    }

    bool is_slave_check = is_slave;
    maintain_is_slave_stats(is_slave_check,
                            current_ts / 1000 /* milliseconds */);

    // Do not insert shardbeats on replicas till they become primary
    if (is_slave_check) {
      static int error_msg_count = 0;
      if ((error_msg_count++ % INFREQUENT_LOGGING == 0 &&
           shardbeat_vlog_level >= 1) ||
          shardbeat_vlog_level >= 3) {
        // NO_LINT_DEBUG
        sql_print_information("is_slave is set. Skipping sharbeats insertion");
      }
      continue;
    }

    // make a copy of the DB trx time. Assuming this map is
    // small around 100 entries max. This is to prevent having
    // to hold LOCK_log
    get_db_trx_times(&db_last_trx_time);

    // initialize things which are not supposed to change in an iteration
    // but are allowed to be updated via global sysvar
    mysql_mutex_lock(&LOCK_shardbeater);
    format_str_comment_iter = format_str_comment;
    skip_databases_iter = skip_databases;
    mysql_mutex_unlock(&LOCK_shardbeater);

    // We fetch dbs each time, as Shard Migrations makes the set dynamic
    std::vector<std::pair<std::string, shard_rs_t>> shards;
    get_dbs_with_filter(sb_thd, skip_databases_iter, &shards);

    for (const auto &dbinfo : shards) {
      check_and_insert_shardbeat(dbinfo, SLA);

      // Watch for shutdown initiation from server
      if (sb_thd->killed) {
        break;
      }
    }
  }

  cleanup_thread();
}

void Shardbeats_manager::cleanup_thread() {
  // NO_LINT_DEBUG
  sql_print_information("Exiting loop of generate_shardbeats");

  transition_to_state(State::STOPPED);

  // Cleanup and exit
  sb_thd->release_resources();

  Global_THD_manager *thd_manager = Global_THD_manager::get_instance();
  thd_manager->remove_thd(sb_thd);
  delete sb_thd;
  sb_thd = nullptr;
}

Shardbeats_manager::Shard_stats::Shard_stats()
    : last_errors(HISTORY_LENGTH),
      last_hbs(HISTORY_LENGTH),
      num_shardbeats(0),
      num_failures(0) {}

void Shardbeats_manager::post_write(const std::string &db,
                                    unsigned long long current_ts) {
  mysql_mutex_lock(&LOCK_shardbeater);
  Shard_stats &stats = db_sb_stats[db];
  if (stats.db.empty()) {
    stats.db = db;
  }

  if (sb_thd->is_error()) {
    if (stats.last_errors.empty() || stats.last_errors.back().mysql_errno !=
                                         sb_thd->get_stmt_da()->mysql_errno()) {
      // A new error type needs to be pushed
      Error_details ed{sb_thd->get_stmt_da()->mysql_errno(), current_ts,
                       current_ts};
      stats.last_errors.push_back(std::move(ed));
    } else {
      stats.last_errors.back().error_time_last = current_ts;
    }
    stats.num_failures++;
  } else {
    stats.last_hbs.push_back(current_ts);
    stats.num_shardbeats++;
  }
  mysql_mutex_unlock(&LOCK_shardbeater);
}

std::string Shardbeats_manager::Error_details::to_string() const {
  char buf[60];
  sprintf(buf, "ErrCode: %u (%llu, %llu)", mysql_errno,
          error_time_start / 1000000, error_time_last / 1000000);
  return buf;
}

std::string Shardbeats_manager::Shard_stats::last_hbs_as_str() const {
  std::vector<std::string> vals;
  for (unsigned long long i : last_hbs) {
    char buf[20];
    sprintf(buf, "%llu", i / 1000000);
    vals.push_back(buf);
  }
  std::stringstream s;
  std::copy(vals.begin(), vals.end(),
            std::ostream_iterator<std::string>(s, ","));
  return s.str();
}

std::string Shardbeats_manager::Shard_stats::last_errors_as_str() const {
  std::vector<std::string> errs;
  for (const Error_details &ed : last_errors) {
    errs.push_back(ed.to_string());
  }

  std::stringstream s;
  std::copy(errs.begin(), errs.end(),
            std::ostream_iterator<std::string>(s, ","));
  return s.str();
}

bool Shardbeats_manager::show_status_cmd(THD *thd) {
  DBUG_ENTER("show_shardbeater_status");
  bool ret = false;
  mem_root_deque<Item *> field_list(thd->mem_root);
  // List<Item> field_list;
  Protocol *protocol = thd->get_protocol();

  field_list.push_back(new Item_empty_string("Running", 3));
  field_list.push_back(new Item_empty_string("User", USERNAME_LENGTH + 1));
  field_list.push_back(new Item_empty_string("Db", 30));
  field_list.push_back(new Item_return_int("Num_OK", 10, MYSQL_TYPE_LONGLONG));
  field_list.push_back(
      new Item_return_int("Num_Fail", 10, MYSQL_TYPE_LONGLONG));

  field_list.push_back(new Item_empty_string("Last_few_inserts", 50));
  field_list.push_back(new Item_empty_string("Last_few_failures", 150));
  field_list.push_back(new Item_return_int("Last Read Only OFF Time(ms)", 10,
                                           MYSQL_TYPE_LONGLONG));
  field_list.push_back(new Item_return_int("Last Read Only ON Time(ms)", 10,
                                           MYSQL_TYPE_LONGLONG));
  field_list.push_back(new Item_return_int("Last Is Not Replica Time(ms)", 10,
                                           MYSQL_TYPE_LONGLONG));
  field_list.push_back(
      new Item_return_int("Last Is Replica Time(ms)", 10, MYSQL_TYPE_LONGLONG));
  field_list.push_back(
      new Item_return_int("Current Time(ms)", 10, MYSQL_TYPE_LONGLONG));

  unsigned long long current_time_ms = my_micro_time() / 1000;
  if (thd->send_result_metadata(field_list,
                                Protocol::SEND_NUM_ROWS | Protocol::SEND_EOF)) {
    ret = true;
    goto err;
  }

  mysql_mutex_lock(&LOCK_shardbeater);
  if (db_sb_stats.empty()) {
    protocol->start_row();

    protocol->store(((state == State::RUNNING) ? "YES" : "NO"),
                    &my_charset_bin);
    protocol->store(shardbeat_user, &my_charset_bin);
    protocol->store("", &my_charset_bin);
    protocol->store(0ULL);
    protocol->store(0ULL);
    protocol->store("", &my_charset_bin);
    protocol->store("", &my_charset_bin);
    protocol->store(ro_OFF_ts);
    protocol->store(ro_ON_ts);
    protocol->store(is_slave_OFF_ts);
    protocol->store(is_slave_ON_ts);
    protocol->store(current_time_ms);
    if (protocol->end_row()) {
      ret = true;
      goto err;
    }
  }

  for (auto &iter : db_sb_stats) {
    const std::string &db = iter.first;
    const Shard_stats &stats = iter.second;
    protocol->start_row();

    protocol->store(((state == State::RUNNING) ? "YES" : "NO"),
                    &my_charset_bin);
    protocol->store(shardbeat_user, &my_charset_bin);
    protocol->store(db.c_str(), &my_charset_bin);
    protocol->store(stats.num_shardbeats);
    protocol->store(stats.num_failures);
    protocol->store(stats.last_hbs_as_str().c_str(), &my_charset_bin);
    protocol->store(stats.last_errors_as_str().c_str(), &my_charset_bin);

    protocol->store(ro_OFF_ts);
    protocol->store(ro_ON_ts);
    protocol->store(is_slave_OFF_ts);
    protocol->store(is_slave_ON_ts);
    protocol->store(current_time_ms);

    if (protocol->end_row()) {
      ret = true;
      goto err;
    }
  }

err:

  my_eof(thd);
  mysql_mutex_unlock(&LOCK_shardbeater);
  DBUG_RETURN(ret);
}

bool Shardbeats_manager::is_running() {
  mysql_mutex_lock(&LOCK_shardbeater);
  bool ret = (state == State::RUNNING);
  mysql_mutex_unlock(&LOCK_shardbeater);
  return ret;
}

void Shardbeats_manager::transition_to_state(Shardbeats_manager::State s) {
  mysql_mutex_lock(&LOCK_shardbeater);
  state = s;
  mysql_cond_broadcast(&COND_shardbeater_run_cond);
  mysql_mutex_unlock(&LOCK_shardbeater);
}

// Fork the shardbeater thread.
bool Shardbeats_manager::start_shardbeater_thread(THD *thd,
                                                  Start_Stop_Reason reason) {
  DBUG_ENTER("start_shardbeater_thread");

  assert(reason == Start_Stop_Reason::NONE ||
         reason == Start_Stop_Reason::READ_ONLY_OFF);
  Security_context *sctx = thd->security_context();
  if (!sctx->check_access(SUPER_ACL)) {
    my_error(ER_SPECIFIC_ACCESS_DENIED_ERROR, MYF(0), "SUPER");
    DBUG_RETURN(true);
  }

  std::string shardbeat_user_str(shardbeat_user);
  if (shardbeat_user_str.empty()) {
    my_error(ER_DISALLOWED_OPERATION, MYF(0), "start shardbeater",
             " when shardbeater user is empty");
    DBUG_RETURN(true);
  }

  std::string table_name(shardbeat_table);
  if (table_name.empty()) {
    my_error(ER_DISALLOWED_OPERATION, MYF(0), "start shardbeater",
             " when shardbeat_table is empty");
    DBUG_RETURN(true);
  }

  mysql_mutex_lock(&LOCK_shardbeater);
  if (state != State::STOPPED) {
    my_error(ER_DISALLOWED_OPERATION, MYF(0), "start shardbeater",
             " when shardbeater thread is running");
    mysql_mutex_unlock(&LOCK_shardbeater);
    DBUG_RETURN(true);
  }

  if (is_slave) {
    // NO_LINT_DEBUG
    sql_print_warning(
        "Shardbeater thread started but instance "
        "is a replica. No shardbeats will be inserted "
        "till promotion");
  }

  // Indicate that it is starting
  state = State::STARTING;
  if (reason == Start_Stop_Reason::READ_ONLY_OFF) {
    current_ro_state = false;
    unsigned long long current_ts = my_micro_time();
    ro_OFF_ts = current_ts / 1000 /* milliseconds */;
  }

  my_thread_handle th;
  if ((mysql_thread_create(0, &th, &connection_attrib, generate_shardbeats,
                           (void *)this))) {
    // NO_LINT_DEBUG
    sql_print_error("Could not create shardbeater thread");
    state = State::STOPPED;
    my_printf_error(ER_UNKNOWN_ERROR, "Shardbeater Thread Creation Error",
                    MYF(0));
    mysql_mutex_unlock(&LOCK_shardbeater);
    DBUG_RETURN(true);
  }

  int start_wait_timeout = 10;
  while (state != State::RUNNING && start_wait_timeout > 0) {
    struct timespec abstime;
    set_timespec(&abstime, 2);

    mysql_cond_timedwait(&COND_shardbeater_run_cond, &LOCK_shardbeater,
                         &abstime);
    start_wait_timeout -= 2;
  }

  bool success = (state == State::RUNNING);

  if (success) {
    my_ok(thd);
  } else {
    my_printf_error(ER_UNKNOWN_ERROR,
                    "Shardbeater Thread didn't start in 10 seconds", MYF(0));
  }
  mysql_mutex_unlock(&LOCK_shardbeater);
  DBUG_RETURN(!success);
}

// set the state to STOPPING and wait for the shardbeater
// thread to exit.
bool Shardbeats_manager::stop_shardbeater_thread(THD *thd,
                                                 Start_Stop_Reason reason) {
  DBUG_ENTER("stop_shardbeater_thread");

  assert(reason == Start_Stop_Reason::NONE ||
         reason == Start_Stop_Reason::READ_ONLY_ON);
  Security_context *sctx = thd->security_context();
  if (!sctx->check_access(SUPER_ACL)) {
    my_error(ER_SPECIFIC_ACCESS_DENIED_ERROR, MYF(0), "SUPER");
    DBUG_RETURN(true);
  }

  mysql_mutex_lock(&LOCK_shardbeater);
  if (state != State::RUNNING) {
    my_error(ER_DISALLOWED_OPERATION, MYF(0), "stop shardbeater",
             " when shardbeater thread is not running");
    mysql_mutex_unlock(&LOCK_shardbeater);
    DBUG_RETURN(true);
  }

  state = State::STOPPING;
  if (reason == Start_Stop_Reason::READ_ONLY_ON) {
    current_ro_state = true;
    unsigned long long current_ts = my_micro_time();
    ro_ON_ts = current_ts / 1000 /* milliseconds */;
  }

  int stop_wait_timeout = 10;
  while (state != State::STOPPED && stop_wait_timeout > 0) {
    struct timespec abstime;
    set_timespec(&abstime, 2);

    mysql_cond_timedwait(&COND_shardbeater_run_cond, &LOCK_shardbeater,
                         &abstime);
    stop_wait_timeout -= 2;
  }

  bool success = (state == State::STOPPED);
  if (success) {
    my_ok(thd);
  } else {
    my_printf_error(ER_UNKNOWN_ERROR,
                    "Shardbeater Thread didn't stop in 10 seconds", MYF(0));
  }
  mysql_mutex_unlock(&LOCK_shardbeater);
  DBUG_RETURN(!success);
}

void Shardbeats_manager::cleanup_mutexes() {
  mysql_mutex_destroy(&LOCK_shardbeater);
  mysql_cond_destroy(&COND_shardbeater_run_cond);
}

void Shardbeats_manager::init_mutexes() {
  mysql_mutex_init(key_LOCK_shardbeater, &LOCK_shardbeater, MY_MUTEX_INIT_FAST);
  mysql_cond_init(key_COND_shardbeater_run_cond, &COND_shardbeater_run_cond);
}

// Make a copy of the map from db -> last_trx_time
// for the shardbeater, so that LOCK_log does not need
// to be held for long.
void Shardbeats_manager::get_db_trx_times(
    std::unordered_map<std::string, unsigned long long> *db_trx_map) {
  mysql_mutex_lock(mysql_bin_log.get_log_lock());
  *db_trx_map = m_last_trx_time;
  mysql_mutex_unlock(mysql_bin_log.get_log_lock());
}

// Update the trx time for a particular shard/db
void Shardbeats_manager::update_db_trx_times(
    THD *thd, unsigned long long oc_flush_timestamp) {
  DBUG_ENTER("MYSQL_BIN_LOG::update_db_trx_time(THD *)");
  mysql_mutex_assert_owner(mysql_bin_log.get_log_lock());

  if (!thd->db().str) {
    DBUG_VOID_RETURN;
  }

  const auto &db_lex_str = thd->db();
  std::string db(db_lex_str.str, db_lex_str.length);

  m_last_trx_time[db] = oc_flush_timestamp;
  DBUG_VOID_RETURN;
}
