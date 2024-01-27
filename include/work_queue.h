/* Copyright (c) 2024 Meta Platforms, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#pragma once

#include <assert.h>

#include <deque>

#include "include/mutex_lock.h"
#include "mysql/service_thd_wait.h"
#include "sql/mysqld.h"
#include "sql/sql_class.h"

/**
  Generic, thread-safe producer/consumer queue.
*/
template <class T>
class Work_queue {
 public:
  Work_queue() {
    mysql_mutex_init(key_LOCK_work_queue, &lock, MY_MUTEX_INIT_FAST);
    mysql_cond_init(key_COND_work_queue, &has_work);
  }

  ~Work_queue() {
    mysql_cond_destroy(&has_work);
    mysql_mutex_destroy(&lock);
  }

  /**
    Indicate to the queue that no more work will be enqueued and that dequeue()
    should just return empty handed.
  */
  void shutdown() { is_shutdown = true; }

  /**
    Enqueue a unit of work on the queue.

    @todo does work need to be shared_ptr copied or will ref do?
  */
  void enqueue(T *work) {
    // Can't enqueue more work on a shutdown queue.
    assert(!is_shutdown);
    MUTEX_LOCK(l, &lock);
    queue.push_back(work);
    mysql_cond_signal(&has_work);
  }

  /**
    Blocking dequeue operation.

    @param thd The THD that is waiting for work. Used for reporting and to check
     for kill signals.

    @return a pointer to the next item from the queue or nullptr if THD is
    killed and queue has been shut down.

    @note Dequeue will still drain queue if it has been shut down, but not if
    THD has been killed. This is important to ensure all issued work is
    completed.
  */
  T *dequeue(THD *thd) {
    T *work{nullptr};
    mysql_mutex_lock(&lock);
    PSI_stage_info old_stage;
    thd->ENTER_COND(&has_work, &lock, &stage_waiting_for_work_item, &old_stage);

    if (queue.empty() && !thd->is_killed()) {
      // Wait for work with timeout.
      do {
        if (is_shutdown) {
          // Queue is empty and shutdown. No more work will be enqueued.
          goto exit;
        }
        const auto timeout_nsec =
            20 * 1000 * 1000;  // 20 ms. TODO: conversion constant/fn?
        struct timespec abstime;
        set_timespec_nsec(&abstime, timeout_nsec);
        thd_wait_begin(thd, THD_WAIT_SLEEP);
        mysql_cond_timedwait(&has_work, &lock, &abstime);
        thd_wait_end(thd);
      } while (queue.empty() && !thd->is_killed());
    }

    assert(!queue.empty());

    work = queue.front();
    queue.pop_front();
  exit:
    mysql_mutex_unlock(&lock);
    thd->EXIT_COND(&old_stage);

    return work;
  }

 private:
  /**
     List of work items.
  */
  std::deque<T *> queue;

  /**
    Lock and condition variable protecting access to the queue.
  */
  mysql_mutex_t lock;
  mysql_cond_t has_work;

  /**
    Signal when no more work will be added to the queue and dequeue attempts
    should give up.
  */
  bool is_shutdown{false};
};
