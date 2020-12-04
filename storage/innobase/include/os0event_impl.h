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

#include <errno.h>
#include <time.h>

#include "ha_prototypes.h"

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

#define BIT63 (1ULL << 63)
#define INC_SIGNAL_COUNT(ev) \
  { ++(ev)->stats; }
#define SIGNAL_COUNT(ev) (static_cast<int64_t>((ev)->stats & ~BIT63))
#define SET_IS_SET(ev) \
  { (ev)->stats |= BIT63; }
#define CLEAR_IS_SET(ev) \
  { (ev)->stats &= ~BIT63; }

#endif /* _WIN32 */

/** Operating system event */
typedef struct os_event_struct os_event_struct_t;
/** Operating system event handle */
typedef os_event_struct_t *os_event_t;

typedef struct os_event_support_struct {
  OSMutex mutex;      /*!< this mutex protects the next
                      fields */
  os_cond_t cond_var; /*!< condition variable is used in
                      waiting for the event */
} os_event_support_t;

extern os_event_support_t *os_support;

/** InnoDB condition variable. */
struct os_event_struct {
  os_event_struct() UNIV_NOTHROW;

  ~os_event_struct() UNIV_NOTHROW;

  friend void os_event_global_init();
  friend void os_event_global_destroy();
  friend void os_support_init(os_event_support_t *ev_sup);
  friend void os_support_destroy(os_event_support_t *ev_sup);
  friend void os_event_create2(os_event_t event);
  friend os_event_t os_event_create();
  friend void os_event_destroy(os_event_t &event);
  friend void os_event_destroy2(os_event_t event);

  /**
  Decreases the refcount of live os_event_struct objects */
  void dec_obj_count() UNIV_NOTHROW {
    ut_ad(n_objects_alive.fetch_sub(1) != 0);
  }

  /** Set the event */
  void set() UNIV_NOTHROW {
    sup->mutex.enter();

    if (!is_set()) {
      broadcast();
    }

    sup->mutex.exit();
  }

  bool try_set() UNIV_NOTHROW {
    if (sup->mutex.try_lock()) {
      if (!is_set()) {
        broadcast();
      }

      sup->mutex.exit();

      return (true);
    }

    return (false);
  }

  int64_t reset() UNIV_NOTHROW {
    sup->mutex.enter();

    if (is_set()) {
      CLEAR_IS_SET(this);
    }

    int64_t ret = SIGNAL_COUNT(this);

    sup->mutex.exit();

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
  bool is_set() const UNIV_NOTHROW { return (this->stats & BIT63) != 0; }

#ifdef UNIV_DEBUG
  /** copy assignment operator impl */
  os_event_struct &operator=(const os_event_struct &other) {
    init_stats();
    sup = other.sup;
    return *this;
  }
#endif

 private:
  /**
  Initialize a condition variable */
  void init_stats() UNIV_NOTHROW {
    /*We return this value in os_event_reset(), which can then be
    be used to pass to the os_event_wait_low(). The value of zero
    is reserved in os_event_wait_low() for the case when the
    caller does not want to pass any signal_count value. To
    distinguish between the two cases we initialize signal_count
    to 1 here. */
    stats = 1;
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

      ret = pthread_cond_wait(&sup->cond_var, sup->mutex);
      ut_a(ret == 0);
    }
#endif /* _WIN32 */
  }

  /**
  Wakes all threads waiting for condition variable */
  void broadcast() UNIV_NOTHROW {
    SET_IS_SET(this);
    INC_SIGNAL_COUNT(this);

#ifdef _WIN32
    WakeAllConditionVariable(&cond_var);
#else
    {
      int ret;

      ret = pthread_cond_broadcast(&sup->cond_var);
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

      ret = pthread_cond_signal(&sup->cond_var);
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
  uint64_t stats;          /*!< msb: "is_set" (boolean bit field)
                           This is TRUE when the event is
                           in the signaled state, i.e., a thread
                           does not stop if it tries to wait for
                           this event */
  os_event_support_t *sup; /*!< Pointer to OS-support data
                           For events created by os_event_create()
                           this will point to an allocated set of
                           data exclusively for this event.
                           For events created by os_event_create2()
                           this will point to one of the shared
                           sync data pool elements
                           allocated by os_event_global_init()
                           */

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

#ifdef UNIV_DEBUG
  static std::atomic_size_t n_objects_alive;
#endif /* UNIV_DEBUG */

 protected:
  // Disable copying
  os_event_struct(const os_event_struct &);
};

#endif /* !os0event_impl_h */
