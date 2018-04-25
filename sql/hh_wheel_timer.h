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

#include <chrono>
#include <mutex>
#include <unordered_map>

#include <boost/dynamic_bitset.hpp>

#include "timer.h"

// This is an implementation of a Hashed, Hierarchical, Wheel Timer, based
// upon this paper:
// http://www.cs.columbia.edu/~nahum/w6998/papers/sosp87-timing-wheels.pdf
//
// The reference implementation for this version is Folly::HHWheelTimer
// https://github.com/facebook/folly/blob/master/folly/io/async/HHWheelTimer.h
//


// Default to Interval being 10 milliseconds (1/100 of a second) and the
// default timeout being 1 Interval
class HHWheelTimer : private Timer {
public:
  using msecs = std::chrono::milliseconds;
  using ID = uint64_t;
  using Tick = uint64_t;
  class Callback;
  using CallbackPtr = std::shared_ptr<Callback>;
  using CallbackVct = std::vector<std::pair<CallbackPtr, ID>>;

  using Bitset = boost::dynamic_bitset<>;
  using BitsetSize = Bitset::size_type;

  class Callback {
  public:
    Callback() = default;
    virtual ~Callback() {
      DBUG_ASSERT(m_wheel_timer == nullptr);
      DBUG_ASSERT(m_expiration_tick == 0);
    }

    // Is this callback scheduled?
    bool isScheduled() const {
      return m_wheel_timer != nullptr;
    }

  protected:
    // timeoutExpired is called when the timeout has expired
    virtual void timeoutExpired(ID id) noexcept = 0;

    // the callback was cancelled.  The default behavior is to call
    // timeoutExpired but this can be overridden if different behavior
    // is needed between timeout and cancelled
    virtual void timeoutCancelled(ID id) noexcept {
      timeoutExpired(id);
    }

  private:
    void setScheduled(HHWheelTimer* wheel, Tick expiration) {
      DBUG_ASSERT(wheel != nullptr);

      DBUG_ASSERT(m_wheel_timer == nullptr);
      DBUG_ASSERT(m_expiration_tick == 0);

      m_wheel_timer = wheel;
      m_expiration_tick = expiration;
    }

    void reset() {
      m_wheel_timer = nullptr;
      m_expiration_tick = 0;
    }

    HHWheelTimer* m_wheel_timer{nullptr};
    Tick m_expiration_tick{0};
    ID m_id{0};
    int32_t m_wheel{-1};
    uint32_t m_bucket{0};

    CallbackPtr m_prev{nullptr};
    CallbackPtr m_next{nullptr};

    friend class HHWheelTimer;
  };

  HHWheelTimer(msecs interval = msecs(1), msecs defaultTimeout = msecs(10)) :
      m_interval(interval),
      m_default_timeout(defaultTimeout),
      m_expire_tick(0),
      m_count(0),
      m_start_time(currTime()),
      m_bitmap(static_cast<BitsetSize>(WHEEL_SIZE)) {
    if (interval == msecs(0) || defaultTimeout == msecs(0)) {
      my_safe_printf_stderr(
          "\nInvalid parameter(s) for HHWheelTimer(%llums, %llums)\n",
          (ulonglong) interval.count(), (ulonglong) defaultTimeout.count());
      abort();
    }

    auto rem = defaultTimeout % interval;
    if (rem != msecs(0)) {
      // If the defaultTimeout is not a multiple of the interval round it up
      m_default_timeout += (interval - rem);
    }
  }

  virtual ~HHWheelTimer() {
    cancelAll();
  }

  // Get the tick interval in milliseconds
  msecs getTickInterval() const {
    return m_interval;
  }

  // Get the default timeout interval in milliseconds
  msecs getDefaultTimeout() const {
    return m_default_timeout;
  }

  // Schedule the specified callback to occur after the specified timeout
  // interval.  If the callback is already scheduled it will be cancelled
  // before being rescheduled.
  ID scheduleTimeout(const CallbackPtr& cb, msecs timeout);

  // Schedule the specified callback to occur after the default timeout
  // interval.  If the callback is already scheduled it will be cancelled
  // before being rescheduled.
  ID scheduleTimeout(const CallbackPtr& cb) {
    return scheduleTimeout(cb, m_default_timeout);
  }

  // Cancel the specfied callback timeout
  bool cancelTimeout(const CallbackPtr& cb);

  // Cancel all outstanding callbacks
  size_t cancelAll();

  // Return the number of pending callbacks
  size_t count() const {
    return m_count;
  }

private:
  // Remove support for copy constructor and assignment operator
  HHWheelTimer(HHWheelTimer const&) = delete;
  HHWheelTimer& operator=(HHWheelTimer const&) = delete;

  // The background thread has woken at the appropriate time - cause all expired
  // timers to have their callback functions called.
  // Receive ownership of the lock from the caller so that we can release it
  // when we are ready.
  // Inherited from Timer
  void expired(std::unique_lock<std::recursive_mutex>&& lock) noexcept override;

  // Remove the callback from the timer
  ID removeCallback(const CallbackPtr& cb);

  // Each wheel uses 8 bits of the tick information.  With 8 wheels we can
  // handle any possible 64-bit time value.  The higher wheels are very
  // unlikely to be used (with a 10ms tick, it takes a timeout over a year
  // away to use the 5th wheel, so the the 6th wheel would require nearly
  // 350 years and the 8th wheel would require nearly 23,000 millenia), but
  // the extra data required is small and it simplifies the code.
  static constexpr uint32_t NUM_WHEELS = 8;
  static constexpr uint32_t WHEEL_BITS = 8;
  static constexpr uint32_t WHEEL_SIZE = (1U << WHEEL_BITS);
  static constexpr uint32_t WHEEL_MASK = (WHEEL_SIZE - 1);

  msecs m_interval;
  msecs m_default_timeout;
  Tick m_expire_tick;
  Tick m_curr_tick;
  size_t m_count;
  std::chrono::steady_clock::time_point m_start_time;

  CallbackPtr m_wheels[NUM_WHEELS][WHEEL_SIZE];

  Bitset m_bitmap;

  // Expire any callbacks in the specified bucket of the lowest wheel
  // If the current tick indicates we have moved around the wheel,
  // cascade callbacks from the upper wheels
  void expireItems(Tick tick, CallbackVct& needNotification);

  // Cascade from upper wheels to lower wheels
  void cascadeTimers(uint32_t wheel, Tick tick, CallbackVct& needNotification);

  // Add the callback to the appropriate wheel
  void addToWheel(const CallbackPtr& cb);
  void readdToWheel(const CallbackPtr& cb, CallbackVct& needNotification);

  // Figure out when we next need to wake up
  void scheduleNextTimeout();

  static std::chrono::steady_clock::time_point currTime() {
    return std::chrono::steady_clock::now();
  }

  Tick currentTick() const {
    auto diff = std::chrono::duration_cast<msecs>(currTime() - m_start_time);
    return diff / m_interval;
  }

  msecs msecsSinceTick(std::chrono::steady_clock::time_point tp) const {
    auto diff = std::chrono::duration_cast<msecs>(tp - m_start_time);
    return diff % m_interval;
  }

  // Cancel all outstanding callbacks, guarded by a mutex
  size_t cancelAllGuarded(CallbackVct& needNotification);

  Tick numTicksRoundUp(msecs tm) {
    return 1 + (tm.count() + (m_interval.count() - 1)) / m_interval.count();
  }

  // link 'node' into the double-linked list pointed to by 'head'
  static void link(CallbackPtr& head, const CallbackPtr& node) {
    DBUG_ASSERT(node->m_prev == nullptr);
    DBUG_ASSERT(node->m_next == nullptr);

    node->m_next = std::move(head);
    if (node->m_next != nullptr) {
      node->m_next->m_prev = node;
    }

    head = node;
  }

  // unlink 'node' from the double-linked list pointed to by 'head'
  static void unlink(CallbackPtr& head, const CallbackPtr& node) {
    if (node->m_prev != nullptr) {
      unlinkImpl(node->m_prev->m_next, node);
    } else {
      DBUG_ASSERT(head == node);
      unlinkImpl(head, node);
    }
  }

  // Unlink this callback from the double-linked list.  The 'ptr' value
  // is either the head or the previous entry's next pointer.
  static void unlinkImpl(CallbackPtr& ptr, const CallbackPtr& node) {
    ptr = std::move(node->m_next);  // nulls m_next
    if (ptr != nullptr) {
      ptr->m_prev = std::move(node->m_prev);  // nulls m_prev
    } else {
      node->m_prev = nullptr;
    }
  }
};
