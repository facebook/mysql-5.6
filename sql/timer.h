/*
   Copyright (c) 2017, Facebook, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#pragma once

#include <atomic>
#include <chrono>
#include <mutex>

#include "my_timer.h"
#include "my_stacktrace.h"

class Timer : public my_timer_t {
public:
  Timer() : m_scheduled(false), m_triggers_in_process(0) {
    int ret = my_timer_create(this);
    if (ret != 0) {
      abort_with_error("Failed to create timer", __FILE__, __LINE__);
    }

    notify_function = Timer::callback;
  }

  ~Timer() {
    my_timer_delete(this);

    // It is possible that a trigger started executing (and blocked on the
    // mutex) before we cancelled all triggers.  This keeps us from
    // destructing this object before that trigger finishes.
    while (m_triggers_in_process > 0) {}

  }

  // Is the timer currently scheduled?
  bool isScheduled() {
    return m_scheduled;
  }

  void abort_with_error(const char *error, const char *file, int line) {
    my_safe_printf_stderr("\n%s at %s:%d\n", error, file, line);
    abort();
  }

  // Schedule the timer for 'ms' milliseconds in the future
  void schedule(std::chrono::milliseconds ms) {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);

    m_scheduled = true;
    int ret = my_timer_set(this, ms.count());
    if (ret != 0) {
      abort_with_error("Failed to set timer (%s:%d)", __FILE__, __LINE__);
    }
  }

  // Cancel any timer for the future
  void cancel() {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);

    if (m_scheduled) {
      int state;
      m_scheduled = false;
      my_timer_cancel(this, &state);
    }
  }

  void trigger() noexcept {
    m_triggers_in_process++;

    {
      // Instead of a standard lock guard, create a unique lock so that we
      // can pass it to the callback which can then unlock it early.
      std::unique_lock<std::recursive_mutex> lock(m_mutex);
      DBUG_ASSERT(lock.owns_lock());

      if (m_scheduled) {
        m_scheduled = false;
        // Pass the guard to the callback so the mutex can be released early
        expired(std::move(lock));
      }
    }

    m_triggers_in_process--;
  }

  // Function to override to handle the timer expiring
  // We will pass ownership of the mutex into the function so the callback
  // can release it when it desires.
  virtual void expired(
      std::unique_lock<std::recursive_mutex>&& lock) noexcept = 0;

protected:
  std::recursive_mutex m_mutex;

private:
  bool m_scheduled;
  std::atomic<size_t> m_triggers_in_process;

  static void callback(my_timer_t *timer);
};
