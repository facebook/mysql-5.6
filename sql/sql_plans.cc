#include "sql_base.h"
#include "sql_show.h"
#include "mysqld.h"
#ifdef HAVE_RAPIDJSON
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#endif
#include <boost/algorithm/string/trim.hpp>
#include "my_md5.h"

/*
  SQL_PLAN

  Provides the execution plan details of SQL statements
*/
#define SQL_PLAN_LENGTH_MAX  8192

ST_FIELD_INFO sql_plan_fields_info[]=
{
  {"PLAN_ID", MD5_BUFF_LENGTH, MYSQL_TYPE_STRING, 0,0,0, SKIP_OPEN_TABLE},
  {"PLAN_LENGTH", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},
  {"PLAN_DATA", SQL_PLAN_LENGTH_MAX, MYSQL_TYPE_STRING, 0,0,0, SKIP_OPEN_TABLE},
  {0, 0, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE}
};

/* Global sql plan hash map to track and update plans in-memory */
std::unordered_map<md5_key, SQL_PLAN*> global_sql_plans;

/*
  The current utilization for the sql plans
*/
ulonglong sql_plans_size = 0;

/*
  It's possible for this mutex to be locked twice by one thread when
  ha_myisam::write_row() errors out during a information schema query.

  We use an error checking mutex here so we can handle this situation.
  More details: https://github.com/facebook/mysql-5.6/issues/132.
*/
static bool lock_sql_plans()
{
/*
  In debug mode safe_mutex is turned on, and
  PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP is ignored.

  However, safe_mutex contains information about the thread ID
  which we can use to determine if we are re-locking the calling thread.

  In release builds pthread_mutex_t is used, which respects the
  PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP property.
*/
#ifndef DBUG_OFF
  if (!pthread_equal(pthread_self(),
                     (&(&LOCK_global_sql_plans)->m_mutex)->thread))
  {
    mysql_mutex_lock(&LOCK_global_sql_plans);
    return false;
  }

  return true;
#else
  return mysql_mutex_lock(&LOCK_global_sql_plans) == EDEADLK;
#endif
}

static void unlock_sql_plans(bool acquired)
{
  /* If lock was already acquired by calling thread, do nothing. */
  if (acquired)
    return;

  /* Otherwise, unlock the mutex */
  mysql_mutex_unlock(&LOCK_global_sql_plans);
}

/*
  free_global_sql_plans
    Frees global_sql_plans map and its content
*/
void free_global_sql_plans(void)
{
  bool lock_acquired = lock_sql_plans();
  for (auto it= global_sql_plans.begin(); it != global_sql_plans.end(); ++it)
  {
    my_free(it->second->plan_data);
    my_free(it->second);
  }
  global_sql_plans.clear();

  sql_plans_size  = 0;

  unlock_sql_plans(lock_acquired);
}

/*
  normalize_plan and friends (array, object, node)

  Traverse the JSON document and remove members that match
  undesired types ("rows", "filtered", "attached_condition",
  "index_condition", "partitions")
*/
#ifdef HAVE_RAPIDJSON
static bool is_excluded_name(const std::string& name)
{
  if (!name.empty() &&
      (name == "rows" ||
       name == "filtered" ||
       name == "partitions" ||
       name == "index_condition" ||
       name == "attached_condition"))
    return true;
  else
    return false;
}

static void normalize_array(rapidjson::Value& node);
static void normalize_object(rapidjson::Value &node);

static void normalize_node(rapidjson::Value &node)
{
  if (node.IsArray())
    normalize_array(node);
  else if (node.IsObject())
    normalize_object(node);
}

static void normalize_object(rapidjson::Value &node)
{
  for (auto childNode = node.MemberBegin(); childNode != node.MemberEnd(); )
  {
    if (is_excluded_name(childNode->name.GetString()))
      childNode = node.RemoveMember(childNode);
    else
    {
      normalize_node(childNode->value);
      ++childNode;
    }
  }
}

static void normalize_array(rapidjson::Value& node)
{
  for (rapidjson::SizeType i = 0; i < node.Size(); ++i)
    normalize_node(node[i]);
}

static void normalize_plan(rapidjson::Value& node)
{
  normalize_node(node);
}
#endif /*HAVE_RAPIDJSON*/

/*
  insert_sql_plan

  Inserts the provided SQL plan: compute the plan ID tne store it in the global
  plan map if it does not exist yet.
*/
void insert_sql_plan(THD *thd, String *json_plan)
{
  if (json_plan->is_empty())
    return;

  /* Check whether we reached the limits for SQL stats and plans
     This is the first check before acquiring the lock on HT
  */
  if (is_sql_stats_collection_above_limit())
    return;

  /* Compute the Plan ID as the MD5 hash of the plan data */
  md5_key plan_id;

#ifdef HAVE_RAPIDJSON
  if (normalized_plan_id)
  {
    memset(plan_id.data(), 0, MD5_HASH_SIZE);

    /* parse JSON plan (string form) and get the JSON document */
    rapidjson::Document plan_doc;
    if (!plan_doc.Parse(json_plan->c_ptr()).HasParseError() &&
        plan_doc.IsObject())
    {
      /* Traverse the JSON document and remove members that match
         undesired types ("rows", "filtered", "attached_condition"
      */
      normalize_plan(plan_doc);

      /* get the normalized plan (document form) as a JSON string */
      rapidjson::StringBuffer plan_buf;
      rapidjson::Writer<rapidjson::StringBuffer> writer(plan_buf);
      if (plan_doc.Accept(writer))
      {
        std::string norm_plan = plan_buf.GetString();
        boost::trim(norm_plan);

        compute_md5_hash((char*) plan_id.data(),
                         norm_plan.c_str(),
                         norm_plan.length());
      }
    }
  }
  else
#endif /*HAVE_RAPIDJSON*/
    compute_md5_hash((char*) plan_id.data(),
                     json_plan->c_ptr(),
                     json_plan->length());

  bool lock_acquired = lock_sql_plans();

  // Check again inside the lock and release the lock if exiting
  if (is_sql_stats_collection_above_limit())
  {
    unlock_sql_plans(lock_acquired);
    return;
  }

  /* Get or create the SQL_PLAN object for this sql statement. */
  SQL_PLAN *sql_plan;
  auto sql_plan_iter= global_sql_plans.find(plan_id);
  if (sql_plan_iter == global_sql_plans.end())
  {
    if (!(sql_plan= ((SQL_PLAN*)my_malloc(sizeof(SQL_PLAN), MYF(MY_WME)))) ||
        !(sql_plan->plan_data= ((char*)my_malloc(MY_MIN(json_plan->length(),
                                                     SQL_PLAN_LENGTH_MAX) + 1,
                                                 MYF(MY_WME)))))
    {
      sql_print_error("Cannot allocate memory for SQL_PLAN.");
      my_free(sql_plan->plan_data);
      my_free(sql_plan);
      unlock_sql_plans(lock_acquired);
      return;
    }

    /* store the original plan length */
    sql_plan->plan_len = json_plan->length();
    /* store truncated plan, up to 8k */
    uint plan_len = MY_MIN(json_plan->length(), SQL_PLAN_LENGTH_MAX);
    memcpy(sql_plan->plan_data, json_plan->c_ptr(), plan_len);
    sql_plan->plan_data[plan_len] = '\0';

    thd->set_plan_id(plan_id.data());

    auto ret= global_sql_plans.emplace(plan_id, sql_plan);
    if (! ret.second)
      DBUG_ASSERT(0);

    sql_plans_size += (MD5_HASH_SIZE + sizeof(SQL_PLAN) + plan_len);
  }
  else
    thd->set_plan_id(sql_plan_iter->first.data());

  unlock_sql_plans(lock_acquired);
}

/* Fills the SQL_PLANS table. */
int fill_sql_plans(THD *thd, TABLE_LIST *tables, Item *cond)
{
  DBUG_ENTER("fill_sql_text");
  TABLE* table= tables->table;

  bool lock_acquired = lock_sql_plans();

  for (auto iter= global_sql_plans.cbegin();
      iter != global_sql_plans.cend(); ++iter)
  {
    int f= 0;
    SQL_PLAN *sql_plan = iter->second;

    restore_record(table, s->default_values);

    /* PLAN ID */
    char plan_id_hex_string[MD5_BUFF_LENGTH];
    array_to_hex(plan_id_hex_string, iter->first.data(), iter->first.size());

    table->field[f++]->store(plan_id_hex_string, MD5_BUFF_LENGTH,
                             system_charset_info);

    /* Plan length */
    table->field[f++]->store(sql_plan->plan_len, TRUE);

    /* Plan data */
    table->field[f++]->store(sql_plan->plan_data,
                             sql_plan->plan_len,
                             system_charset_info);

    if (schema_table_store_record(thd, table))
    {
      unlock_sql_plans(lock_acquired);
      DBUG_RETURN(-1);
    }
  }
  unlock_sql_plans(lock_acquired);

  DBUG_RETURN(0);
}
