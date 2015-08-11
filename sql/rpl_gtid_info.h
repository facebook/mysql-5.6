#ifndef RPL_GTID_INFO_H
#define RPL_GTID_INFO_H

#ifdef HAVE_REPLICATION

#include <my_global.h>
#include <sql_priv.h>

#include "my_sys.h"
#include "rpl_gtid.h"
#include "rpl_info_handler.h"
#include "rpl_info.h"

class Gtid_info : public Rpl_info
{
friend class Rpl_info_factory;

private:
  uint id; // Internal id of the row in the slave_gitd_info table.
  char database_name[65];
  // Last gtid seen in the replication stream of the database_name.
  char last_gtid_string[Gtid::MAX_TEXT_LENGTH + 1];
  // Used to store the sidno and gno of the last_gtid_string.
  Gtid last_gtid;
  // Local sid_map of this class to compare Uuid strings.
  Sid_map *sid_map;
public:
  inline void set_database_name(const char* db_arg)
  {
    strmake(database_name, db_arg, sizeof(database_name) -1);
  }

  inline void set_last_gtid(const char* gtid_string)
  {
    strmake(last_gtid_string, gtid_string, sizeof(last_gtid_string) - 1);
  }

  inline const char *get_database_name()
  {
    return database_name;
  }

  inline const char *get_last_gtid_string()
  {
    return last_gtid_string;
  }

  static size_t get_number_info_gtid_fields();
  int gtid_init_info();
  bool flush_info(bool force);
  void end_info();
  bool skip_event(const char *gtid);

  Gtid_info(uint param_id
#ifdef HAVE_PSI_INTERFACE
            ,PSI_mutex_key *param_key_info_run_lock,
            PSI_mutex_key *param_key_info_data_lock,
            PSI_mutex_key *param_key_info_sleep_lock,
            PSI_mutex_key *param_key_info_thd_lock,
            PSI_mutex_key *param_key_info_data_cond,
            PSI_mutex_key *param_key_info_start_cond,
            PSI_mutex_key *param_key_info_stop_cond,
            PSI_mutex_key *param_key_info_sleep_cond
#endif
            );
  Gtid_info(const Gtid_info& info);
  Gtid_info& operator = (const Gtid_info& info);
  virtual ~Gtid_info();

private:
  bool read_info(Rpl_info_handler *from);
  bool write_info(Rpl_info_handler *to);
};

#endif /* HAVE_REPLICATION */
#endif /* RPL_GTID_INFO_H */
