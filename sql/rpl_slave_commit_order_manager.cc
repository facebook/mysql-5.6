/* Copyright (c) 2014, 2015, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#include "rpl_slave_commit_order_manager.h"

#include "rpl_mi.h"
#include "rpl_rli.h"
#include "rpl_rli_pdb.h"     // Slave_worker
#include "mysqld.h"          // key_commit_order_manager_mutex ..

Commit_order_manager::Commit_order_manager(uint32 worker_numbers)
  : m_workers(worker_numbers)
{
  m_rollback_trx.store(false);
  mysql_mutex_init(key_commit_order_manager_mutex, &m_queue_mutex, NULL);
  for (uint32 i= 0; i < worker_numbers; i++)
  {
    mysql_cond_init(key_commit_order_manager_cond, &m_workers[i].cond, NULL);
    m_workers[i].status= OCS_FINISH;
  }
}

void Commit_order_manager::register_trx(Slave_worker *worker)
{
  DBUG_ENTER("Commit_order_manager::register_trx");

  mysql_mutex_lock(&m_queue_mutex);
  const auto db= worker->get_current_db();

  DBUG_ASSERT(m_workers[worker->id].status == OCS_FINISH);
  m_workers[worker->id].status= OCS_WAIT;
  queue_push(db, worker->id);

  mysql_mutex_unlock(&m_queue_mutex);
  DBUG_VOID_RETURN;
}

/**
  Waits until it becomes the queue head.

  @param[in] worker The worker which is executing the transaction.
  @param[in] all    If it is a real transation commit.

  @retval false All previous threads succeeded so this thread can go
  ahead and commit.
*/
bool Commit_order_manager::wait_for_its_turn(Slave_worker *worker,
                                                  bool all)
{
  DBUG_ENTER("Commit_order_manager::wait_for_its_turn");

  DBUG_ASSERT(m_workers[worker->id].status == OCS_WAIT ||
              m_workers[worker->id].status == OCS_FINISH);

  /*
    When prior transaction fail, current trx should stop and wait for signal
    to rollback itself
  */
  if ((all || ending_single_stmt_trans(worker->info_thd, all) ||
       m_rollback_trx.load())
      && m_workers[worker->id].status == OCS_WAIT)
  {
    PSI_stage_info old_stage;
    mysql_cond_t *cond= &m_workers[worker->id].cond;
    THD *thd= worker->info_thd;
    const auto db= worker->get_current_db();

    DBUG_PRINT("info", ("Worker %lu is waiting for commit signal", worker->id));

    mysql_mutex_lock(&m_queue_mutex);
    thd->ENTER_COND(cond, &m_queue_mutex,
                    &stage_worker_waiting_for_its_turn_to_commit,
                    &old_stage);

    while (queue_front(db) != worker->id)
    {
      if (unlikely(worker->found_order_commit_deadlock()))
      {
        thd->EXIT_COND(&old_stage);
        DBUG_RETURN(true);
      }
      mysql_cond_wait(cond, &m_queue_mutex);
    }

    m_workers[worker->id].status= OCS_SIGNAL;

    thd->EXIT_COND(&old_stage);

    if (m_rollback_trx.load())
    {
      unregister_trx(worker);

      DBUG_PRINT("info", ("thd has seen an error signal from old thread"));
      thd->get_stmt_da()->set_overwrite_status(true);
      my_error(ER_SLAVE_WORKER_STOPPED_PREVIOUS_THD_ERROR, MYF(0));
    }
  }

  DBUG_RETURN(m_rollback_trx.load());
}

void Commit_order_manager::unregister_trx(Slave_worker *worker)
{
  DBUG_ENTER("Commit_order_manager::unregister_trx");

  if (m_workers[worker->id].status == OCS_SIGNAL)
  {
    DBUG_PRINT("info", ("Worker %lu is signalling next transaction",
                         worker->id));

    const auto db= worker->get_current_db();

    mysql_mutex_lock(&m_queue_mutex);

    /* Set next manager as the head and signal the trx to commit. */
    queue_pop(db);
    if (!queue_empty(db))
      mysql_cond_signal(&m_workers[queue_front(db)].cond);

    m_workers[worker->id].status= OCS_FINISH;

    mysql_mutex_unlock(&m_queue_mutex);
  }

  DBUG_VOID_RETURN;
}

void Commit_order_manager::report_rollback(Slave_worker *worker)
{
  DBUG_ENTER("Commit_order_manager::report_rollback");

  // Without resetting order commit deadlock flag wait_for_its_turn will return
  // immediately without changing state and due to that unregister_trx() will be
  // a no-op.
  worker->reset_order_commit_deadlock();

  (void) wait_for_its_turn(worker, true);
  /* No worker can set m_rollback_trx unless it is its turn to commit */
  m_rollback_trx.store(true);
  unregister_trx(worker);

  DBUG_VOID_RETURN;
}

void Commit_order_manager::report_deadlock(Slave_worker *worker)
{
  DBUG_ENTER("Commit_order_manager::report_deadlock");
  THD *thd= worker->info_thd;
  mysql_mutex_lock(&thd->LOCK_thd_data);
  ++slave_commit_order_deadlocks;
  worker->report_order_commit_deadlock();
  /* Let's signal to any wait loop this worker is executing, this will also
   * cover the wait loop in wait_for_its_turn() above.
   * NOTE: we just want to send a signal without changing the killed flag
   */
  thd->awake(thd->killed);
  mysql_mutex_unlock(&thd->LOCK_thd_data);
  DBUG_VOID_RETURN;
}

