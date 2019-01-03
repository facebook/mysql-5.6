/* Copyright (c) 2014, 2016, Oracle and/or its affiliates. All rights reserved.

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

#ifndef RPL_SLAVE_COMMIT_ORDER_MANAGER
#define RPL_SLAVE_COMMIT_ORDER_MANAGER

#include "my_global.h"
#include "rpl_rli_pdb.h"    // get_thd_worker


class Commit_order_manager
{
public:
  Commit_order_manager(uint32 worker_numbers);
  ~Commit_order_manager() {}

  /**
    Register the worker into commit order queue when coordinator dispatches a
    transaction to the worker.
    @param[in] worker The worker which the transaction will be dispatched to.
  */
  void register_trx(Slave_worker *worker);

  /**
    Wait for its turn to commit or unregister.

    @param[in] worker The worker which is executing the transaction.
    @param[in] all    If it is a real transation commit.

    @return
      @retval false  All previous transactions succeed, so this transaction can
                     go ahead and commit.
      @retval true   One or more previous transactions rollback, so this
                     transaction should rollback.
  */
  bool wait_for_its_turn(Slave_worker *worker, bool all);

  /**
    Unregister the transaction from the commit order queue and signal the next
    one to go ahead.

    @param[in] worker The worker which is executing the transaction.
  */
  void unregister_trx(Slave_worker *worker);
  /**
    Wait its turn to rollback and report the rollback.

    @param[in] worker The worker which is executing the transaction.
  */
  void report_rollback(Slave_worker *worker);

  /**
    Wait its turn to unregister and unregister the transaction. It is called for
    the cases that trx is already committed, but nothing is binlogged.

    @param[in] worker The worker which is executing the transaction.
  */
  void report_commit(Slave_worker *worker)
  {
    DBUG_ASSERT(!m_rollback_trx.load());
    wait_for_its_turn(worker, true);
    unregister_trx(worker);
  }

  void report_deadlock(Slave_worker *worker);
private:
  enum order_commit_status
  {
    OCS_WAIT,   // worker is waiting for it's turn after registering
    OCS_SIGNAL, // worker is done waiting, will unregister next
    OCS_FINISH  // ready for next registration (steady state), post unreg
  };

  struct worker_info
  {
    uint32 next;
    mysql_cond_t cond;
    enum order_commit_status status;
  };

  mysql_mutex_t m_queue_mutex;
  std::atomic<bool> m_rollback_trx;

  /* It stores order commit information of all workers. */
  std::vector<worker_info> m_workers;
  /*
    They are used to construct a transaction queue per DB with trx_info::next
    together.  both head and tail point to a slot of m_trx_vector, when the
    queue is not empty, otherwise their value are QUEUE_EOF.
  */
  std::unordered_map<std::string, uint32> queue_heads;
  std::unordered_map<std::string, uint32> queue_tails;
  static const uint32 QUEUE_EOF= 0xFFFFFFFF;

  uint32 queue_head(const std::string& db) const
  {
    mysql_mutex_assert_owner(&m_queue_mutex);
    const auto elem= queue_heads.find(db);
    return (elem == queue_heads.end() ? QUEUE_EOF : elem->second);
  }

  uint32 queue_tail(const std::string& db) const
  {
    mysql_mutex_assert_owner(&m_queue_mutex);
    const auto elem= queue_tails.find(db);
    return (elem == queue_tails.end() ? QUEUE_EOF : elem->second);
  }

  bool queue_empty(const std::string& db) const
  {
    mysql_mutex_assert_owner(&m_queue_mutex);
    return queue_head(db) == QUEUE_EOF;
  }

  void queue_pop(const std::string& db)
  {
    mysql_mutex_assert_owner(&m_queue_mutex);
    DBUG_ASSERT(!queue_empty(db));
    queue_heads[db]= m_workers[queue_head(db)].next;
    if (queue_head(db) == QUEUE_EOF)
      queue_tails[db]= QUEUE_EOF;
  }

  void queue_push(const std::string& db, uint32 index)
  {
    mysql_mutex_assert_owner(&m_queue_mutex);
    DBUG_ASSERT(index < m_workers.size());
    if (queue_head(db) == QUEUE_EOF)
      queue_heads[db]= index;
    else
      m_workers[queue_tail(db)].next= index;
    queue_tails[db]= index;
    m_workers[index].next= QUEUE_EOF;
  }

  uint32 queue_front(const std::string& db) const
  {
    mysql_mutex_assert_owner(&m_queue_mutex);
    return queue_head(db);
  }
};

#endif /*RPL_SLAVE_COMMIT_ORDER_MANAGER*/
