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

#include "hh_wheel_timer.h"

#include <future>

HHWheelTimer::ID HHWheelTimer::scheduleTimeout(
    const CallbackPtr& cb, msecs timeout) {
  // Grab the mutex to proect the data structures
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  if (cb->isScheduled()) {
    // Cancel the callback if it is scheduled
    removeCallback(cb);
  }

  // Calculate how many ticks in the future to trigger this timeout
  // (rounding up)
  Tick ticks = numTicksRoundUp(timeout);
  m_curr_tick = currentTick();

  // Add the callback
  cb->setScheduled(this, m_curr_tick + ticks);
  addToWheel(cb);
  m_count++;

  // Determine when next we need to wake
  scheduleNextTimeout();

  return ++(cb->m_id);
}

// Cancel the specfied callback timeout
bool HHWheelTimer::cancelTimeout(const CallbackPtr& cb) {
  ID id = 0;
  bool cancelled = false;

  {
    // Grab the mutex to protect the data structures
    std::lock_guard<std::recursive_mutex> guard(m_mutex);

    if (cb->isScheduled()) {
      // Remove the callback from the wheel
      id = removeCallback(cb);
      cancelled = true;
    }
  }

  // If we successfully cancelled the callback, call the timeoutCancelled()
  // function outside of the mutex.
  if (cancelled) {
    cb->timeoutCancelled(id);
  }

  return cancelled;
}

HHWheelTimer::ID
HHWheelTimer::removeCallback(const CallbackPtr& cb) {
  // Decrement the number of items in the wheel and cancel any outstanding
  // alarms if there are no items left.
  if (--m_count <= 0) {
    DBUG_ASSERT(m_count == 0);
    cancel();
  }

  // Remove the callback
  if (cb->m_wheel != -1) {
    auto& head = m_wheels[cb->m_wheel][cb->m_bucket];
    unlink(head, cb);

    // If we are on wheel 0 and there are no entries left in this bucket,
    // reset the bit for this bucket to 0.
    if (cb->m_wheel == 0 && head == nullptr) {
      m_bitmap.reset(cb->m_bucket);
    }
  }

  cb->reset();
  return cb->m_id;
}

void HHWheelTimer::addToWheel(const CallbackPtr& cb) {
  // If the expire tick is less than 256 ticks away, it can go on the lowest
  // wheel otherwise it needs to go on a higher wheel.
  Tick ticks = cb->m_expiration_tick;
  Tick diff = ticks - m_curr_tick;
  cb->m_wheel = 0;
  while (diff >= WHEEL_SIZE) {
    diff >>= WHEEL_BITS;
    ticks >>= WHEEL_BITS;
    cb->m_wheel++;
  }

  DBUG_ASSERT((uint32_t) cb->m_wheel < NUM_WHEELS);

  cb->m_bucket = ticks & WHEEL_MASK;
  if (cb->m_wheel == 0) {
    m_bitmap.set(cb->m_bucket);
  }

  // Link the callback to the appropriate bucket
  link(m_wheels[cb->m_wheel][cb->m_bucket], cb);
}

// Add the callback to the appropriate wheel
void HHWheelTimer::readdToWheel(const CallbackPtr& cb,
                                CallbackVct& needNotification) {
  // Sometimes under a load, m_curr_tick can be passed the ticks for this
  // callback - just remove it.
  if (cb->m_expiration_tick <= m_curr_tick) {
    cb->m_wheel = -1;
    needNotification.emplace_back(cb, removeCallback(cb));
  } else {
    addToWheel(cb);
  }
}

// The background thread has woken at the appropriate time - cause all expired
// timers to have their callback functions called
// Inherited from Timer
void HHWheelTimer::expired(
    std::unique_lock<std::recursive_mutex>&& lock) noexcept {
  CallbackVct needNotification;

  // Validate that the mutex is locked
  DBUG_ASSERT(lock.owns_lock());

  m_curr_tick = currentTick();

  // In case we have moved past the expected expire tick, expire all items
  // from then to the current tick.  If we have woken early nothing will
  // be processed and we will wait again.
  auto tick = m_expire_tick;
  while (tick <= m_curr_tick) {
    expireItems(tick++, needNotification);
  }

  // Determine when next we need to wake
  scheduleNextTimeout();

  // Release the mutex - the caller assumes we will do this
  lock.unlock();
  DBUG_ASSERT(!lock.owns_lock());

  // Notify all the callbacks that expired (outside the mutex)
  for (const auto& itr : needNotification) {
    itr.first->timeoutExpired(itr.second);
  }
}

// Expire any callbacks in the specified bucket of the lowest wheel
// If the current tick indicates we have moved around the wheel,
// cascade callbacks from the upper wheels
void HHWheelTimer::expireItems(Tick tick, CallbackVct& needNotification) {
  // Access the list of callbacks in the correct bucket
  uint32_t bucket = tick & WHEEL_MASK;
  const CallbackPtr& head = m_wheels[0][bucket];

  // Iterate through all callbacks
  while (head != nullptr) {
    CallbackPtr cb = head;
    needNotification.emplace_back(cb, removeCallback(cb));
  }

  DBUG_ASSERT(!m_bitmap.test(bucket));

  // If we have moved around to the zero bucket again, cascade from the next
  // wheel up
  if (bucket == 0) {
    cascadeTimers(1, tick >> WHEEL_BITS, needNotification);
  }
}

size_t HHWheelTimer::cancelAllGuarded(CallbackVct& needNotification) {
  // Grab the mutex to protect the data structures
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  // If nothing to do just return the empty map
  if (m_count == 0) {
    return 0;
  }

  size_t count = 0;

  // Iterate over all callbacks in all buckets in all wheels
  for (auto& wheel : m_wheels) {
    for (auto& bucket : wheel) {
      while (bucket != nullptr) {
        // We only need to grab the front of the list as each callback will be
        // removed from the list each time.
        CallbackPtr cb = bucket;

        // Add the callback and current ID to the map of callbacks that need
        // notification.
        needNotification.emplace_back(cb, removeCallback(cb));
        count++;

        // We have removed all callbacks; short-circuit the loops
        if (m_count == 0) {
          return count;
        }
      }
    }
  }

  // We shouldn't ever get here as when we remove the last callback we should
  // have immediately returned
  assert(false);
  return count;
}

// Cancel all outstanding callbacks
size_t HHWheelTimer::cancelAll() {
  CallbackVct needNotification;

  // Cancel all the callbacks, updating the needNotification map with which
  // need notification
  auto count = cancelAllGuarded(needNotification);

  // Notify each callback that it was cancelled.
  for (const auto& itr : needNotification) {
    itr.first->timeoutCancelled(itr.second);
  }

  return count;
}

// Cascade from upper wheels to lower wheels
void HHWheelTimer::cascadeTimers(uint32_t wheel, Tick tick,
                                 CallbackVct& needNotification) {
  DBUG_ASSERT(wheel < NUM_WHEELS);

  uint32_t bucket = tick & WHEEL_MASK;

  // For each callback in the bucket, remove it from the list and re-add it
  // (presumably to a lower wheel).
  auto& head = m_wheels[wheel][bucket];
  while (head != nullptr) {
    auto cb = head;
    unlink(head, cb);
    readdToWheel(cb, needNotification);
  }

  // If we are back on entry 0 and we are not on the highest wheel, cascade
  // entries down from the next wheel up
  if (bucket == 0 && wheel < (NUM_WHEELS - 1)) {
    cascadeTimers(wheel + 1, tick >> WHEEL_BITS, needNotification);
  }
}

// Figure out when we next need to wake up
void HHWheelTimer::scheduleNextTimeout() {
  uint32_t tick = 1;

  if (m_count > 0) {
    // Calculate how many ticks away the bucket containing the next callback
    // is or when we would wrap back to 0 - when we need to wake anyway to
    // handle cascading from upper wheels.
    auto bucket = (m_curr_tick + 1) & WHEEL_MASK;
    if (bucket != 0) {
      auto pos = m_bitmap.find_next(bucket - 1);
      // Find the next tick where we have to do anything
      if (pos == Bitset::npos) {
        tick = WHEEL_SIZE - bucket; // No further bits are set
      } else {
        tick += pos - bucket; // move to the next bucket that has a bit set
      }
    }

    // If we are not currently scheduled or the next time to wake is sooner
    // than we are already scheduled for, schedule a new timer
    if (!isScheduled() || (m_expire_tick > m_curr_tick + tick)) {
      // Schedule to be woken in 'tick' ticks
      DBUG_ASSERT(tick > 0);
      schedule(m_interval * tick - msecsSinceTick(currTime()));

      // Update our value for when we expect to be awoken next
      m_expire_tick = tick + m_curr_tick;
    }
  } else if (isScheduled()) {
    // Cancel any outstanding timers as we have nothing pending
    cancel();
  }
}
