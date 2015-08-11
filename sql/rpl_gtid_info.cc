#include <my_global.h>
#include "rpl_gtid_info.h"

#ifdef HAVE_REPLICATION

// The columns of slave_gtid_info table.
const char *info_gtid_fields []=
{
  "id",
  "database_name",
  "last_gtid"
};

Gtid_info::Gtid_info(uint param_id
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
                    )
  : Rpl_info("GTID",
#ifdef HAVE_PSI_INTERFACE
             param_key_info_run_lock, param_key_info_data_lock,
             param_key_info_sleep_lock, param_key_info_thd_lock,
             param_key_info_data_cond, param_key_info_start_cond,
             param_key_info_stop_cond, param_key_info_sleep_cond,
#endif
             param_id + 1), id(param_id)
{
  DBUG_ASSERT(internal_id == id + 1);
  database_name[0] = 0;
  last_gtid_string[0] = 0;
  sid_map = new Sid_map(NULL);
  last_gtid.sidno = last_gtid.gno = 0;
}

Gtid_info::~Gtid_info()
{
  end_info();
  delete sid_map;
}

/**
   Flushes the gtid state of this database.
   @param[in] force Forces the synchronization.

   @return false Success
           true  Failure
*/
bool Gtid_info::flush_info(bool force)
{
  DBUG_ENTER("Gtid_info::flush_info");
  if (!inited)
    DBUG_RETURN(0);

  handler->set_sync_period(sync_relayloginfo_period);

  if (!handler->need_write(force))
    DBUG_RETURN(0);

  if (write_info(handler))
    goto err;

  if (handler->flush_info(force))
    goto err;

  DBUG_RETURN(false);

err:
  sql_print_information("Error writing Gtid %s into "
                        "mysql.slave_gtid_info table", last_gtid_string);
  DBUG_RETURN(true);
}

/**
   Prepares and writes the state of gtid_info to the stable storage.
   @param[in] to Write handler of the table.

   @return false Success
           true  Failure
*/
bool Gtid_info::write_info(Rpl_info_handler *to)
{
  DBUG_ENTER("Gtid_info::write_info");
  if (to->prepare_info_for_write() ||
      to->set_info((int) internal_id) ||
      to->set_info(database_name) ||
      to->set_info(last_gtid_string))
    DBUG_RETURN(true);

  DBUG_RETURN(false);
}

// Reads from the table storage and stores the state.
// @param[in] from Read handler of the table.
//
// @return false Success
//         true  Failure
bool Gtid_info::read_info(Rpl_info_handler *from)
{
  DBUG_ENTER("Gtid_info:read_info");
  int temp_internal_id;
  if (from->prepare_info_for_read())
    DBUG_RETURN(true);
  if (from->get_info((int *) &temp_internal_id, internal_id) ||
      from->get_info(database_name, (size_t) sizeof(database_name),
                     (char *) "") ||
      from->get_info(last_gtid_string, (size_t) sizeof(last_gtid_string),
                     (char *) ""))
    DBUG_RETURN(true);
  internal_id = temp_internal_id;
  if (last_gtid_string[0] != 0 &&
      last_gtid.parse(sid_map, last_gtid_string) != RETURN_STATUS_OK)
    DBUG_RETURN(true);

  DBUG_RETURN(false);
}

// Closes the access to the repository.
void Gtid_info::end_info()
{
  DBUG_ENTER("Gtid_info::end_info");
  if (!inited)
    DBUG_VOID_RETURN;
  if (handler)
    handler->end_info();
  inited = 0;
  DBUG_VOID_RETURN;
}

int Gtid_info::gtid_init_info()
{
  DBUG_ENTER("Gtid_info::gtid_init_info");
  enum_return_check check_return= ERROR_CHECKING_REPOSITORY;

  if (inited)
    DBUG_RETURN(0);

  if ((check_return= check_info()) == ERROR_CHECKING_REPOSITORY)
    goto err;

  if (handler->init_info())
    goto err;

  handler->set_sync_period(1);

  if (read_info(handler))
    goto err;

  inited = 1;

  DBUG_RETURN(0);

err:
  handler->end_info();
  inited = 0;
  sql_print_error("Error reading gtid info configuration.");
  DBUG_RETURN(1);
}

size_t Gtid_info::get_number_info_gtid_fields()
{
  return sizeof(info_gtid_fields)/sizeof(info_gtid_fields[0]);
}

/**
   Decides whether to skip the current event or not.
   If the current_gtid of group encountered in replication stream is
   less than the last_gtid stored in the stable storage, then the event
   should be skipped.
   @param[in] current_gtid Gtid of the current group

   @return true  If the event should be skipped.
           false If the event should not be skipped.
*/
bool Gtid_info::skip_event(const char *current_gtid)
{
  DBUG_ENTER("Gtid_info::skip_event");
  Gtid gtid;
  gtid.sidno = gtid.gno = 0;
  if (*current_gtid == 0)
    DBUG_RETURN(false);
  DBUG_ASSERT(sid_map);
  gtid.parse(sid_map, current_gtid);
  if (last_gtid.sidno == gtid.sidno &&
      last_gtid.gno >= gtid.gno && gtid.gno != 0)
    DBUG_RETURN(true);
  DBUG_RETURN(false);
}

#endif
