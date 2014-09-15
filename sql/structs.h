#ifndef STRUCTS_INCLUDED
#define STRUCTS_INCLUDED

/* Copyright (c) 2000, 2013, Oracle and/or its affiliates. All rights reserved.

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



/* The old structures from unireg */

#include "sql_plugin.h"                         /* plugin_ref */
#include "sql_const.h"                          /* MAX_REFLENGTH */
#include "my_time.h"                   /* enum_mysql_timestamp_type */
#include "thr_lock.h"                  /* thr_lock_type */
#include "my_base.h"                   /* ha_rows, ha_key_alg */
#include "mysql_com.h"                 /* NAME_LEN */
#include "mysqld.h"                    /* my_io_perf */
#include "atomic_stat.h"

struct TABLE;
class Field;

class THD;

typedef struct st_date_time_format {
  uchar positions[8];
  char  time_separator;			/* Separator between hour and minute */
  uint flag;				/* For future */
  LEX_STRING format;
} DATE_TIME_FORMAT;


typedef struct st_keyfile_info {	/* used with ha_info() */
  uchar ref[MAX_REFLENGTH];		/* Pointer to current row */
  uchar dupp_ref[MAX_REFLENGTH];	/* Pointer to dupp row */
  uint ref_length;			/* Length of ref (1-8) */
  uint block_size;			/* index block size */
  File filenr;				/* (uniq) filenr for table */
  ha_rows records;			/* Records i datafilen */
  ha_rows deleted;			/* Deleted records */
  ulonglong data_file_length;		/* Length off data file */
  ulonglong max_data_file_length;	/* Length off data file */
  ulonglong index_file_length;
  ulonglong max_index_file_length;
  ulonglong delete_length;		/* Free bytes */
  ulonglong auto_increment_value;
  int errkey,sortkey;			/* Last errorkey and sorted by */
  time_t create_time;			/* When table was created */
  time_t check_time;
  time_t update_time;
  ulong mean_rec_length;		/* physical reclength */
} KEYFILE_INFO;


class KEY_PART_INFO {	/* Info about a key part */
public:
  Field *field;
  uint	offset;				/* offset in record (from 0) */
  uint	null_offset;			/* Offset to null_bit in record */
  /* Length of key part in bytes, excluding NULL flag and length bytes */
  uint16 length;
  /*
    Number of bytes required to store the keypart value. This may be
    different from the "length" field as it also counts
     - possible NULL-flag byte (see HA_KEY_NULL_LENGTH)
     - possible HA_KEY_BLOB_LENGTH bytes needed to store actual value length.
  */
  uint16 store_length;
  uint16 key_type;
  uint16 fieldnr;			/* Fieldnum in UNIREG */
  uint16 key_part_flag;			/* 0 or HA_REVERSE_SORT */
  uint8 type;
  uint8 null_bit;			/* Position to null_bit */
  void init_from_field(Field *fld);     /** Fill data from given field */
  void init_flags();                    /** Set key_part_flag from field */
};


typedef struct st_key {
  /** Tot length of key */
  uint	key_length;
  /** dupp key and pack flags */
  ulong flags;
  /** dupp key and pack flags for actual key parts */
  ulong actual_flags;
  /** How many key_parts */
  uint  user_defined_key_parts;
  /** How many key_parts including hidden parts */
  uint  actual_key_parts;
  /**
     Key parts allocated for primary key parts extension but
     not used due to some reasons(no primary key, duplicated key parts)
  */
  uint  unused_key_parts;
  /** Should normally be = key_parts */
  uint	usable_key_parts;
  uint  block_size;
  enum  ha_key_alg algorithm;
  /**
    Note that parser is used when the table is opened for use, and
    parser_name is used when the table is being created.
  */
  union
  {
    /** Fulltext [pre]parser */
    plugin_ref parser;
    /** Fulltext [pre]parser name */
    LEX_STRING *parser_name;
  };
  KEY_PART_INFO *key_part;
  /** Name of key */
  char	*name;
  /**
    Array of AVG(#records with the same field value) for 1st ... Nth key part.
    0 means 'not known'.
    For temporary heap tables this member is NULL.
  */
  ulong *rec_per_key;
  union {
    int  bdb_return_if_eq;
  } handler;
  TABLE *table;
  LEX_STRING comment;
} KEY;


struct st_join_table;

typedef struct st_reginfo {		/* Extra info about reg */
  struct st_join_table *join_tab;	/* Used by SELECT() */
  enum thr_lock_type lock_type;		/* How database is used */
  bool not_exists_optimize;
  /*
    TRUE <=> range optimizer found that there is no rows satisfying
    table conditions.
  */
  bool impossible_range;
} REGINFO;


/*
  Originally MySQL used MYSQL_TIME structure inside server only, but since
  4.1 it's exported to user in the new client API. Define aliases for
  new names to keep existing code simple.
*/

typedef enum enum_mysql_timestamp_type timestamp_type;


typedef struct {
  ulong year,month,day,hour;
  ulonglong minute,second,second_part;
  bool neg;
} INTERVAL;


typedef struct st_known_date_time_format {
  const char *format_name;
  const char *date_format;
  const char *datetime_format;
  const char *time_format;
} KNOWN_DATE_TIME_FORMAT;

extern const char *show_comp_option_name[];

typedef int *(*update_var)(THD *, struct st_mysql_show_var *);

typedef struct	st_lex_user {
  LEX_STRING user, host, password, plugin, auth;
  bool uses_identified_by_clause;
  bool uses_identified_with_clause;
  bool uses_authentication_string_clause;
  bool uses_identified_by_password_clause;
} LEX_USER;

/*
  This structure specifies the maximum amount of resources which
  can be consumed by each account. Zero value of a member means
  there is no limit.
*/
typedef struct user_resources {
  /* Maximum number of queries/statements per hour. */
  uint questions;
  /*
     Maximum number of updating statements per hour (which statements are
     updating is defined by sql_command_flags array).
  */
  uint updates;
  /* Maximum number of connections established per hour. */
  uint conn_per_hour;
  /* Maximum number of concurrent connections. */
  uint user_conn;
  /*
     Values of this enum and specified_limits member are used by the
     parser to store which user limits were specified in GRANT statement.
  */
  enum {QUERIES_PER_HOUR= 1, UPDATES_PER_HOUR= 2, CONNECTIONS_PER_HOUR= 4,
        USER_CONNECTIONS= 8};
  uint specified_limits;
} USER_RESOURCES;

#define USER_STATS_MAGIC 0x17171717

/** Counts resources consumed per-user.
    Data is exported via IS.user_statistics.
*/
typedef struct st_user_stats {
  my_io_perf_atomic_t	io_perf_read;
  my_io_perf_atomic_t	io_perf_read_blob;
  my_io_perf_atomic_t	io_perf_read_primary;
  my_io_perf_atomic_t	io_perf_read_secondary;
  atomic_stat<ulonglong> bytes_received;
  atomic_stat<ulonglong> bytes_sent;
  atomic_stat<ulonglong> binlog_bytes_written;
  atomic_stat<ulonglong> commands_ddl;
  atomic_stat<ulonglong> commands_delete;
  atomic_stat<ulonglong> commands_handler;
  atomic_stat<ulonglong> commands_insert;
  atomic_stat<ulonglong> commands_other;
  atomic_stat<ulonglong> commands_select;
  atomic_stat<ulonglong> commands_transaction;
  atomic_stat<ulonglong> commands_update;
  atomic_stat<ulonglong> connections_denied_max_global; // over global limit
  atomic_stat<ulonglong> connections_denied_max_user;   // over per user limit
  atomic_stat<ulonglong> connections_lost;              // closed on error
  atomic_stat<ulonglong> connections_total;             // total conns created
  atomic_stat<ulonglong> errors_access_denied;          // ..to table or db
  atomic_stat<ulonglong> errors_total;
  atomic_stat<ulonglong> microseconds_wall;
  atomic_stat<ulonglong> microseconds_ddl;
  atomic_stat<ulonglong> microseconds_delete;
  atomic_stat<ulonglong> microseconds_handler;
  atomic_stat<ulonglong> microseconds_insert;
  atomic_stat<ulonglong> microseconds_other;
  atomic_stat<ulonglong> microseconds_select;
  atomic_stat<ulonglong> microseconds_transaction;
  atomic_stat<ulonglong> microseconds_update;
  atomic_stat<ulonglong> queries_empty;
  atomic_stat<ulonglong> rows_deleted;
  atomic_stat<ulonglong> rows_fetched;
  atomic_stat<ulonglong> rows_inserted;
  atomic_stat<ulonglong> rows_read;
  atomic_stat<ulonglong> rows_updated;

  /* see variables of same name in ha_statistics */
  atomic_stat<ulonglong> rows_index_first;
  atomic_stat<ulonglong> rows_index_next;

  atomic_stat<ulonglong> transactions_commit;
  atomic_stat<ulonglong> transactions_rollback;

#ifndef DBUG_OFF
  uint magic;
#endif // !DBUG_OFF

  /* TODO(mcallaghan) -- failed_queries, disk IO, parse and records_in_range
     seconds, slow queries. I also want to count connections that fail
     authentication but the hash_user_connections key is (user,host) and when
     auth fails you know which user/host the login provided but you don't know
     which pair it wanted to use. Read the docs for how auth uses mysql.user
     table for more details. When auth failure occurs mysqld doesn't have
     a referenced to a USER_STATS entry. This probably requires another hash
     table keyed only by the login name.
     Others:
       errors_lock_wait_timeout, errors_deadlock
       queries_slow
  */
} USER_STATS;

/*
   Hack to provide stats for SQL replication slave as THD::user_connect is
   not set for it. See get_user_stats.
*/
extern USER_STATS slave_user_stats;

/*
   Hack to provide stats for anything that doesn't have THD::user_connect except
   the SQL slave.  See get_user_stats.
*/
extern USER_STATS other_user_stats;

/*
  This structure is used for counting resources consumed and for checking
  them against specified user limits.
*/
typedef struct  user_conn {
  /*
     Pointer to user+host key (pair separated by '\0') defining the entity
     for which resources are counted (By default it is user account thus
     priv_user/priv_host pair is used. If --old-style-user-limits option
     is enabled, resources are counted for each user+host separately).
  */
  char *user;
  /* Pointer to host part of the key. */
  char *host;
  /**
     The moment of time when per hour counters were reset last time
     (i.e. start of "hour" for conn_per_hour, updates, questions counters).
  */
  ulonglong reset_utime;
  /* Total length of the key. */
  uint len;
  /* Current amount of concurrent connections for this account. */
  uint connections;
  /*
     Current number of connections per hour, number of updating statements
     per hour and total number of statements per hour for this account.
  */
  uint conn_per_hour, updates, questions;

  /* Maximum amount of resources which account is allowed to consume. */
  USER_RESOURCES user_resources;
  /*
    Counts resources consumed for this user.
    Use thd_get_user_stats(THD*) rather than USER_CONN::user_stats directly
  */
  USER_STATS user_stats;
} USER_CONN;

typedef struct st_table_stats {
  char db[NAME_LEN + 1];     /* [db] + '\0' */
  char table[NAME_LEN + 1];  /* [table] + '\0' */
  /* Hash table key, table->s->table_cache_key for the table */
  char hash_key[NAME_LEN * 2 + 2];
  int hash_key_len;          /* table->s->key_length for the table */

  atomic_stat<ulonglong> queries_used;	/* number of times used by a query */

  atomic_stat<ulonglong> rows_inserted;	/* Number of rows inserted */
  atomic_stat<ulonglong> rows_updated;	/* Number of rows updated */
  atomic_stat<ulonglong> rows_deleted;	/* Number of rows deleted */
  atomic_stat<ulonglong> rows_read;	/* Number of rows read for this table */
  atomic_stat<ulonglong> rows_requested;/* Number of row read attempts for
                                         this table.  This counts requests
                                         that do not return a row. */

  comp_stats_atomic_t comp_stats;        /* Compression statistics */

  page_stats_atomic_t page_stats;       /* per page type statistics */

  /* See variables of same name in ha_statistics */
  atomic_stat<ulonglong> rows_index_first;
  atomic_stat<ulonglong> rows_index_next;

  my_io_perf_atomic_t io_perf_read;	/* Read IO performance counters */
  my_io_perf_atomic_t io_perf_write;	/* Write IO performance counters */
  my_io_perf_atomic_t io_perf_read_blob;/* Blob read IO performance counters */
  my_io_perf_atomic_t io_perf_read_primary;/* Read IO performance counters for
                                              primary index */
  my_io_perf_atomic_t io_perf_read_secondary;/* Read IO performance counters for
                                                secondary index */
  atomic_stat<ulonglong> index_inserts;	/* Number of secondary index inserts. */
  atomic_stat<ulonglong> queries_empty;	/* Number of non-join empty queries */

  const char* engine_name;
} TABLE_STATS;

	/* Bits in form->update */
#define REG_MAKE_DUPP		1	/* Make a copy of record when read */
#define REG_NEW_RECORD		2	/* Write a new record if not found */
#define REG_UPDATE		4	/* Uppdate record */
#define REG_DELETE		8	/* Delete found record */
#define REG_PROG		16	/* User is updating database */
#define REG_CLEAR_AFTER_WRITE	32
#define REG_MAY_BE_UPDATED	64
#define REG_AUTO_UPDATE		64	/* Used in D-forms for scroll-tables */
#define REG_OVERWRITE		128
#define REG_SKIP_DUP		256

/**
  Flags for TABLE::status (maximum 8 bits). Do NOT add new ones.
  @todo: GARBAGE and NOT_FOUND could be unified. UPDATED and DELETED could be
  changed to "bool current_row_has_already_been_modified" in the
  multi_update/delete objects (one such bool per to-be-modified table).
  @todo aim at removing the status. There should be more local ways.
*/
#define STATUS_GARBAGE          1
/**
   Means we were searching for a row and didn't find it. This is used by
   storage engines (@see handler::index_read_map()) and the Server layer.
*/
#define STATUS_NOT_FOUND        2
/// Reserved for use by multi-table update. Means the row has been updated.
#define STATUS_UPDATED          16
/**
   Means that table->null_row is set. This is an artificial NULL-filled row
   (one example: in outer join, if no match has been found in inner table).
*/
#define STATUS_NULL_ROW         32
/// Reserved for use by multi-table delete. Means the row has been deleted.
#define STATUS_DELETED          64

/*
  Such interval is "discrete": it is the set of
  { auto_inc_interval_min + k * increment,
    0 <= k <= (auto_inc_interval_values-1) }
  Where "increment" is maintained separately by the user of this class (and is
  currently only thd->variables.auto_increment_increment).
  It mustn't derive from Sql_alloc, because SET INSERT_ID needs to
  allocate memory which must stay allocated for use by the next statement.
*/
class Discrete_interval {
private:
  ulonglong interval_min;
  ulonglong interval_values;
  ulonglong  interval_max;    // excluded bound. Redundant.
public:
  Discrete_interval *next;    // used when linked into Discrete_intervals_list

  /// Determine if the given value is within the interval
  bool in_range(const ulonglong value) const
  {
    return  ((value >= interval_min) && (value < interval_max));
  }

  void replace(ulonglong start, ulonglong val, ulonglong incr)
  {
    interval_min=    start;
    interval_values= val;
    interval_max=    (val == ULONGLONG_MAX) ? val : start + val * incr;
  }
  Discrete_interval(ulonglong start, ulonglong val, ulonglong incr) :
    next(NULL) { replace(start, val, incr); };
  Discrete_interval() : next(NULL) { replace(0, 0, 0); };
  ulonglong minimum() const { return interval_min;    };
  ulonglong values()  const { return interval_values; };
  ulonglong maximum() const { return interval_max;    };
  /*
    If appending [3,5] to [1,2], we merge both in [1,5] (they should have the
    same increment for that, user of the class has to ensure that). That is
    just a space optimization. Returns 0 if merge succeeded.
  */
  bool merge_if_contiguous(ulonglong start, ulonglong val, ulonglong incr)
  {
    if (interval_max == start)
    {
      if (val == ULONGLONG_MAX)
      {
        interval_values=   interval_max= val;
      }
      else
      {
        interval_values+=  val;
        interval_max=      start + val * incr;
      }
      return 0;
    }
    return 1;
  };
};

/// List of Discrete_interval objects
class Discrete_intervals_list {

/**
   Discrete_intervals_list objects are used to remember the
   intervals of autoincrement values that have been used by the
   current INSERT statement, so that the values can be written to the
   binary log.  However, the binary log can currently only store the
   beginning of the first interval (because WL#3404 is not yet
   implemented).  Hence, it is currently not necessary to store
   anything else than the first interval, in the list.  When WL#3404 is
   implemented, we should change the '# define' below.
*/
#define DISCRETE_INTERVAL_LIST_HAS_MAX_ONE_ELEMENT 1

private:
  /**
    To avoid heap allocation in the common case when there is only one
    interval in the list, we store the first interval here.
  */
  Discrete_interval        first_interval;
  Discrete_interval        *head;
  Discrete_interval        *tail;
  /**
    When many intervals are provided at the beginning of the execution of a
    statement (in a replication slave or SET INSERT_ID), "current" points to
    the interval being consumed by the thread now (so "current" goes from
    "head" to "tail" then to NULL).
  */
  Discrete_interval        *current;
  uint                  elements;               ///< number of elements
  void operator=(Discrete_intervals_list &);    // prevent use of this
  bool append(Discrete_interval *new_interval)
  {
    if (unlikely(new_interval == NULL))
      return true;
    DBUG_PRINT("info",("adding new auto_increment interval"));
    if (head == NULL)
      head= current= new_interval;
    else
      tail->next= new_interval;
    tail= new_interval;
    elements++;
    return false;
  }
  void copy_shallow(const Discrete_intervals_list *other)
  {
    const Discrete_interval *o_first_interval= &other->first_interval;
    first_interval= other->first_interval;
    head= other->head == o_first_interval ? &first_interval : other->head;
    tail= other->tail == o_first_interval ? &first_interval : other->tail;
    current=
      other->current == o_first_interval ? &first_interval : other->current;
    elements= other->elements;
  }
  Discrete_intervals_list(const Discrete_intervals_list &other)
  { copy_shallow(&other); }

public:
  Discrete_intervals_list()
    : head(NULL), tail(NULL), current(NULL), elements(0) {}
  void empty()
  {
    if (head)
    {
      // first element, not on heap, should not be delete-d; start with next:
      for (Discrete_interval *i= head->next; i;)
      {
#ifdef DISCRETE_INTERVAL_LIST_HAS_MAX_ONE_ELEMENT
        DBUG_ASSERT(0);
#endif
        Discrete_interval *next= i->next;
        delete i;
        i= next;
      }
    }
    head= tail= current= NULL;
    elements= 0;
  }
  void swap(Discrete_intervals_list *other)
  {
    const Discrete_intervals_list tmp(*other);
    other->copy_shallow(this);
    copy_shallow(&tmp);
  }
  const Discrete_interval *get_next()
  {
    const Discrete_interval *tmp= current;
    if (current != NULL)
      current= current->next;
    return tmp;
  }
  ~Discrete_intervals_list() { empty(); };
  /**
    Appends an interval to the list.

    @param start  start of interval
    @val   how    many values it contains
    @param incr   what increment between each value
    @retval true  error
    @retval false success
  */
  bool append(ulonglong start, ulonglong val, ulonglong incr)
  {
    // If there are no intervals, add one.
    if (head == NULL)
    {
      first_interval.replace(start, val, incr);
      return append(&first_interval);
    }
    // If this interval can be merged with previous, do that.
    if (tail->merge_if_contiguous(start, val, incr) == 0)
      return false;
    // If this interval cannot be merged, append it.
#ifdef DISCRETE_INTERVAL_LIST_HAS_MAX_ONE_ELEMENT
    /*
      We cannot create yet another interval as we already contain one. This
      situation can happen. Assume innodb_autoinc_lock_mode>=1 and
       CREATE TABLE T(A INT AUTO_INCREMENT PRIMARY KEY) ENGINE=INNODB;
       INSERT INTO T VALUES (NULL),(NULL),(1025),(NULL);
      Then InnoDB will reserve [1,4] (because of 4 rows) then
      [1026,1026]. Only the first interval is important for
      statement-based binary logging as it tells the starting point. So we
      ignore the second interval:
    */
    return false;
#else
    return append(new Discrete_interval(start, val, incr));
#endif
  }
  ulonglong minimum()     const { return (head ? head->minimum() : 0); };
  ulonglong maximum()     const { return (head ? tail->maximum() : 0); };
  uint      nb_elements() const { return elements; }
};

#endif /* STRUCTS_INCLUDED */
