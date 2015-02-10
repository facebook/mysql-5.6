#ifndef SQL_READONLY_INCLUDED
#define SQL_READONLY_INCLUDED

#include "sql_acl.h"   /* SUPER_ACL */
#include "sql_class.h" /* THD class */
#include "mysqld.h"    /* opt_readonly and opt_super_readonly */

static inline bool check_ro(THD *thd)
{
  return (
    opt_readonly && (
      !(thd->security_ctx->master_access & SUPER_ACL) ||
      (opt_super_readonly && !(thd->slave_thread))
    )
  );
}

#endif /* SQL_READONLY_INCLUDED */
