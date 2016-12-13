/* Copyright (c) 2016, Percona and/or its affiliates. All rights reserved.

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

#ifndef _io_perf_h_
#define _io_perf_h_

#include "atomic_stat.h"
#include <algorithm>

#ifdef	__cplusplus
extern "C" {
#endif

/* Per-table operation and IO statistics */

/* Struct used for IO performance counters within a single thread */
struct my_io_perf_struct {
  ulonglong bytes;
  ulonglong requests;
  ulonglong svc_time; /*!< time to do read or write operation */
  ulonglong svc_time_max;
  ulonglong wait_time; /*!< total time in the request array */
  ulonglong wait_time_max;
  ulonglong slow_ios; /*!< requests that take too long */

  /* Initialize a my_io_perf_t struct. */
  inline void init() {
    memset(this, 0, sizeof(*this));
  }

  /* Sets this to a - b in diff */
  inline void diff(const my_io_perf_struct& a, const my_io_perf_struct& b) {
    if (a.bytes > b.bytes)
      bytes = a.bytes - b.bytes;
    else
      bytes = 0;

    if (a.requests > b.requests)
      requests = a.requests - b.requests;
    else
      requests = 0;

    if (a.svc_time > b.svc_time)
      svc_time = a.svc_time - b.svc_time;
    else
      svc_time = 0;

    if (a.wait_time > b.wait_time)
      wait_time = a.wait_time - b.wait_time;
    else
      wait_time = 0;

    if (a.slow_ios > b.slow_ios)
      slow_ios = a.slow_ios - b.slow_ios;
    else
      slow_ios = 0;

    svc_time_max = std::max(a.svc_time_max, b.svc_time_max);
    wait_time_max = std::max(a.wait_time_max, b.wait_time_max);
  }

  /* Accumulates io perf values */
  inline void sum(const my_io_perf_struct& that) {
    bytes += that.bytes;
    requests += that.requests;
    svc_time += that.svc_time;
    svc_time_max = std::max(svc_time_max, that.svc_time_max);
    wait_time += that.wait_time;
    wait_time_max = std::max(wait_time_max, that.wait_time_max);
    slow_ios += that.slow_ios;
  }
};
typedef struct my_io_perf_struct my_io_perf_t;

/* Struct used for IO performance counters, shared among multiple threads */
struct my_io_perf_atomic_struct {
  atomic_stat<ulonglong> bytes;
  atomic_stat<ulonglong> requests;
  atomic_stat<ulonglong> svc_time; /*!< time to do read or write operation */
  atomic_stat<ulonglong> svc_time_max;
  atomic_stat<ulonglong> wait_time; /*!< total time in the request array */
  atomic_stat<ulonglong> wait_time_max;
  atomic_stat<ulonglong> slow_ios; /*!< requests that take too long */

  /* Initialize an my_io_perf_atomic_t struct. */
  inline void init() {
    bytes.clear();
    requests.clear();
    svc_time.clear();
    svc_time_max.clear();
    wait_time.clear();
    wait_time_max.clear();
    slow_ios.clear();
  }

  /* Accumulates io perf values using atomic operations */
  inline void sum(const my_io_perf_struct& that) {
    bytes.inc(that.bytes);
    requests.inc(that.requests);

    svc_time.inc(that.svc_time);
    wait_time.inc(that.wait_time);

    // In the unlikely case that two threads attempt to update the max
    // value at the same time, only the first will succeed.  It's possible
    // that the second thread would have set a larger max value, but we
    // would rather error on the side of simplicity and avoid looping the
    // compare-and-swap.
    svc_time_max.set_max_maybe(that.svc_time);
    wait_time_max.set_max_maybe(that.wait_time);

    slow_ios.inc(that.slow_ios);
  }

  /* These assignments allow for races. That is OK. */
  inline void set_maybe(const my_io_perf_struct& that) {
    bytes.set_maybe(that.bytes);
    requests.set_maybe(that.requests);
    svc_time.set_maybe(that.svc_time);
    svc_time_max.set_maybe(that.svc_time_max);
    wait_time.set_maybe(that.wait_time);
    wait_time_max.set_maybe(that.wait_time_max);
    slow_ios.set_maybe(that.slow_ios);
  }
};
typedef struct my_io_perf_atomic_struct my_io_perf_atomic_t;

#ifdef	__cplusplus
}
#endif
#endif
