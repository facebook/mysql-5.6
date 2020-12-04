/*****************************************************************************
Copyright (c) 1995, 2019, Oracle and/or its affiliates. All Rights Reserved.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License, version 2.0, as published by the
Free Software Foundation.

This program is also distributed with certain software (including but not
limited to OpenSSL) that is licensed under separate terms, as designated in a
particular file or component or in included license documentation. The authors
of MySQL hereby grant you an additional permission to link the program and
your derivative works with the separately licensed software that they have
included with MySQL.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License, version 2.0,
for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA

*****************************************************************************/

/** @file include/os0event_impl.h
 The definition of operating system condition variables

 *******************************************************/

#ifndef os0event_impl_h
#define os0event_impl_h

#include "os0event.h"

#include <errno.h>
#include <time.h>

#include "ha_prototypes.h"
#include "ut0mutex.h"
#include "ut0new.h"

#ifdef _WIN32
#include <windows.h>
#endif /* _WIN32 */

/** The number of microseconds in a second. */
static const uint64_t MICROSECS_IN_A_SECOND = 1000000;

/** The number of nanoseconds in a second. */
static const uint64_t NANOSECS_IN_A_SECOND [[maybe_unused]] =
    1000 * MICROSECS_IN_A_SECOND; 

#ifdef _WIN32
/** Native condition variable. */
typedef CONDITION_VARIABLE os_cond_t;
#else
/** Native condition variable */
typedef pthread_cond_t os_cond_t;
#endif /* _WIN32 */

/** InnoDB condition variable. */
struct os_event {
  os_event() UNIV_NOTHROW;

  ~os_event() UNIV_NOTHROW;

  friend void os_event_global_init();
  friend void os_event_global_destroy();

  /**
  Destroys a condition variable */
  void destroy() UNIV_NOTHROW {
#ifndef _WIN32
    int ret = pthread_cond_destroy(&cond_var);
    ut_a(ret == 0);
#endif /* !_WIN32 */

    mutex.destroy();

    ut_ad(n_objects_alive.fetch_sub(1) != 0);
  }

  /** Set the event */
  void set() UNIV_NOTHROW {
    mutex.enter();

    if (!m_set) {
      broadcast();
    }

    mutex.exit();
  }

  bool try_set() UNIV_NOTHROW {
    if (mutex.try_lock()) {
      if (!m_set) {
        broadcast();
      }

      mutex.exit();

      return (true);
    }

    return (false);
  }

  int64_t reset() UNIV_NOTHROW {
    mutex.enter();

    if (m_set) {
      m_set = false;
    }

    int64_t ret = signal_count;

    mutex.exit();

    return (ret);
  }

  /**
  Waits for an event object until it is in the signaled state.

  Typically, if the event has been signalled after the os_event_reset()
  we'll return immediately because event->m_set == true.
  There are, however, situations (e.g.: sync_array code) where we may
  lose this information. For example:

  thread A calls os_event_reset()
  thread B calls os_event_set()   [event->m_set == true]
  thread C calls os_event_reset() [event->m_set == false]
  thread A calls os_event_wait()  [infinite wait!]
  thread C calls os_event_wait()  [infinite wait!]

  Where such a scenario is possible, to avoid infinite wait, the
  value returned by reset() should be passed in as
  reset_sig_count. */
  void wait_low(int64_t reset_sig_count) UNIV_NOTHROW;

  /** Waits for an event object until it is in the signaled state or
  a timeout is exceeded.
  @param  time_in_usec    Timeout, or std::chrono::microseconds::max()
  @param  reset_sig_count Zero or the value returned by previous call of
  os_event_reset().
  @return 0 if success, OS_SYNC_TIME_EXCEEDED if timeout was exceeded */
  ulint wait_time_low(std::chrono::microseconds timeout,
                      int64_t reset_sig_count) UNIV_NOTHROW;

  /** @return true if the event is in the signalled state. */
  bool is_set() const UNIV_NOTHROW { return (m_set); }

 private:
  /**
  Initialize a condition variable */
  void init() UNIV_NOTHROW {
    mutex.init();

#ifdef _WIN32
    InitializeConditionVariable(&cond_var);
#else
    {
      int ret;

      ret = pthread_cond_init(&cond_var, &cond_attr);
      ut_a(ret == 0);
    }
#endif /* _WIN32 */

    ut_d(n_objects_alive.fetch_add(1));
  }

  /**
  Wait on condition variable */
  void wait() UNIV_NOTHROW {
#ifdef _WIN32
    if (!SleepConditionVariableCS(&cond_var, mutex, INFINITE)) {
      ut_error;
    }
#else
    {
      int ret;

      ret = pthread_cond_wait(&cond_var, mutex);
      ut_a(ret == 0);
    }
#endif /* _WIN32 */
  }

  /**
  Wakes all threads waiting for condition variable */
  void broadcast() UNIV_NOTHROW {
    m_set = true;
    ++signal_count;

#ifdef _WIN32
    WakeAllConditionVariable(&cond_var);
#else
    {
      int ret;

      ret = pthread_cond_broadcast(&cond_var);
      ut_a(ret == 0);
    }
#endif /* _WIN32 */
  }

  /**
  Wakes one thread waiting for condition variable */
  void signal() UNIV_NOTHROW {
#ifdef _WIN32
    WakeConditionVariable(&cond_var);
#else
    {
      int ret;

      ret = pthread_cond_signal(&cond_var);
      ut_a(ret == 0);
    }
#endif /* _WIN32 */
  }

  /**
  Do a timed wait on condition variable.
  @return true if timed out, false otherwise */
  bool timed_wait(
#ifndef _WIN32
      const timespec *abstime /*!< Timeout. */
#else
      DWORD time_in_ms /*!< Timeout in milliseconds. */
#endif /* !_WIN32 */
  );

#ifndef _WIN32
  /** Returns absolute time until which we should wait if
  we wanted to wait for timeout since now.
  This method could be removed if we switched to the usage
  of std::condition_variable. */
  struct timespec get_wait_timelimit(std::chrono::microseconds timeout);
#endif /* !_WIN32 */

 private:
  bool m_set;           /*!< this is true when the
                        event is in the signaled
                        state, i.e., a thread does
                        not stop if it tries to wait
                        for this event */
  int64_t signal_count; /*!< this is incremented
                        each time the event becomes
                        signaled */
  EventMutex mutex;     /*!< this mutex protects
                        the next fields */

  os_cond_t cond_var; /*!< condition variable is
                      used in waiting for the event */

#ifndef _WIN32
  /** Attributes object passed to pthread_cond_* functions.
  Defines usage of the monotonic clock if it's available.
  Initialized once, in the os_event::global_init(), and
  destroyed in the os_event::global_destroy(). */
  static pthread_condattr_t cond_attr;

  /** True iff usage of the monotonic clock has been successfully
  enabled for the cond_attr object. */
  static bool cond_attr_has_monotonic_clock;
#endif /* !_WIN32 */
  static bool global_initialized;

#ifdef UNIV_DEBUG
  static std::atomic_size_t n_objects_alive;
#endif /* UNIV_DEBUG */

 protected:
  // Disable copying
  os_event(const os_event &);
  os_event &operator=(const os_event &);
};

#endif /* !os0event_impl_h */
