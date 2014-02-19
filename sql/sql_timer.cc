/* Copyright (c) 2012, Twitter, Inc. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA. */

#include "sql_class.h"      /* THD */
#include "sql_timer.h"      /* thd_timer_set, etc. */
#include "my_timer.h"       /* my_timer_t */

#ifndef DBUG_OFF
#define IF_DBUG(expr)   expr
#else
#define IF_DBUG(expr)
#endif

struct st_thd_timer
{
  THD *thd;
  my_timer_t timer;
  pthread_mutex_t mutex;
};

C_MODE_START
static void thd_timer_notify_function(my_timer_t *);
C_MODE_END

/**
  Allocate and initialize a thread timer object.

  @return NULL on failure.
*/

static thd_timer_t *
thd_timer_create(void)
{
  thd_timer_t *ttp;
  DBUG_ENTER("thd_timer_create");

  if (! (ttp= (thd_timer_t *) my_malloc(sizeof(*ttp), MYF(MY_WME))))
    DBUG_RETURN(NULL);

  IF_DBUG(ttp->thd= NULL);

  ttp->timer.notify_function= thd_timer_notify_function;
  pthread_mutex_init(&ttp->mutex, MY_MUTEX_INIT_FAST);

  if (! my_timer_create(&ttp->timer))
    DBUG_RETURN(ttp);

  pthread_mutex_destroy(&ttp->mutex);
  my_free(ttp);

  DBUG_RETURN(NULL);
}


/**
  Release resources allocated for a thread timer.

  @param  ttp   Thread timer object.
*/

static void
thd_timer_destroy(thd_timer_t *ttp)
{
  DBUG_ENTER("thd_timer_destroy");

  my_timer_delete(&ttp->timer);
  my_free(ttp);

  DBUG_VOID_RETURN;
}


/**
  Terminate the statement that a thread (session) is currently executing.

  @param  thd   Thread (session) context.
*/

static void
kill_thread(THD *thd)
{
  mysql_mutex_lock(&thd->LOCK_thd_data);
  thd->awake(THD::KILL_TIMEOUT);
  mysql_mutex_unlock(&thd->LOCK_thd_data);
}


/**
  Timer expiration notification callback.

  @param  timer   Timer (mysys) object.

  @note Invoked in a separate thread of control.
*/

static void
thd_timer_notify_function(my_timer_t *timer)
{
  thd_timer_t *ttp= my_container_of(timer, thd_timer_t, timer);

  pthread_mutex_lock(&ttp->mutex);

  /*
    Statement might have finished while the timer notification
    was being delivered. If this is the case, the timer object
    was detached (orphaned) and has no associted session (thd).
  */
  bool attached= ttp->thd;

  if (attached)
  {
    kill_thread(ttp->thd);
    /* Mark the notification as delivered. */
    ttp->thd= NULL;
  }

  pthread_mutex_unlock(&ttp->mutex);

  if (! attached)
    thd_timer_destroy(ttp);
}


/**
  Set the time until the currently running statement is aborted.

  @param  thd   Thread (session) context.
  @param  ttp   Thread timer object.
  @param  time  Length of time, in milliseconds, until the currently
                running statement is aborted.

  @return NULL on failure.
*/

thd_timer_t *
thd_timer_set(THD *thd, thd_timer_t *ttp, unsigned long time)
{
  DBUG_ENTER("thd_timer_set");

  DBUG_ASSERT(ttp == NULL || ttp->thd == NULL);

  /* Attempt to reuse an existing thread timer object. */
  if (ttp == NULL)
    ttp= thd_timer_create();

  if (ttp == NULL)
    DBUG_RETURN(NULL);

  /* Mark the notification as pending. */
  ttp->thd= thd;

  /* Arm the timer. */
  if (! my_timer_set(&ttp->timer, time))
    DBUG_RETURN(ttp);

  /* Dispose of the (cached) timer object. */
  thd_timer_destroy(ttp);

  DBUG_RETURN(NULL);
}


/**
  Deactivate the given timer.

  @param  ttp   Thread timer object.

  @return NULL if the timer object was orphaned.
          Otherwise, the given timer object is returned.
*/

thd_timer_t *
thd_timer_reset(thd_timer_t *ttp)
{
  _Bool state= false;
  DBUG_ENTER("thd_timer_reset");

  my_timer_reset(&ttp->timer, &state);

  /*
    The timer object can be reused if the timer was stopped before
    expiring. Otherwise, the timer notification function might be
    executing asynchronously in the context of a separate thread.
  */
  if (state)
  {
    IF_DBUG(ttp->thd= NULL);
    DBUG_RETURN(ttp);
  }

  pthread_mutex_lock(&ttp->mutex);
  /* Check if the notification function has already been invoked. */
  bool const pending= ttp->thd;
  /* Mark the timer object as orphaned. */
  ttp->thd= NULL;
  pthread_mutex_unlock(&ttp->mutex);

  /*
    If the notification function has already finished running, cache
    the timer object as there are no outstanding references to it.
  */
  DBUG_RETURN(pending ? NULL : ttp);
}


/**
  Release resources allocated for a given thread timer.

  @param  ttp   Thread timer object.
*/

void
thd_timer_end(thd_timer_t *ttp)
{
  DBUG_ENTER("thd_timer_end");

  thd_timer_destroy(ttp);

  DBUG_VOID_RETURN;
}

