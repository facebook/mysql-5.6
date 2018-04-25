#include <atomic>
#include <chrono>
#include <random>
#include <thread>

#include <gtest/gtest.h>

#include "hh_wheel_timer.h"

#include "../gunit/thread_utils.h"

#if defined(UNIV_DEBUG_VALGRIND)
#include "valgrind/valgrind.h"
#endif

namespace hh_wheel_timer_unittests {

using seconds = std::chrono::seconds;
using msecs = std::chrono::milliseconds;

constexpr std::chrono::milliseconds k60Seconds(60000);

class CallbackTest : public HHWheelTimer::Callback {
public:
  CallbackTest() :
      setID_{0},
      expiredID_{0},
      cancelledID_{0},
      expired_{false},
      cancelled_{false} {
    start_ = std::chrono::steady_clock::now();
  }

  void timeoutExpired(HHWheelTimer::ID id) noexcept override {
    end_ = std::chrono::steady_clock::now();
    expiredID_ = id;
    expired_ = true;
  }

  void timeoutCancelled(HHWheelTimer::ID id) noexcept override {
    end_ = std::chrono::steady_clock::now();
    cancelledID_ = id;
    cancelled_ = true;
  }

  msecs duration() {
    return std::chrono::duration_cast<msecs>(end_ - start_);
  }

  msecs wait(msecs to) {
    bool printed = false;
    auto last_secs = seconds(0);
    while (!expired_ && !cancelled_) {
      if (to >= msecs(1000)) {
        auto diff = std::chrono::steady_clock::now() - start_;
        auto secs = std::chrono::duration_cast<seconds>(diff);
        if (to.count() > 1000 && secs != last_secs) {
          printf(
              "    [Waited %u of %u seconds]\r",
              (uint32_t) secs.count(),
              (uint32_t) (to.count() / 1000));
          fflush(stdout);
          last_secs = secs;
          printed = true;
        }
      }
      std::this_thread::sleep_for(msecs(1));
    }

    if (printed) {
      printf("                                       \r");
    }

    return duration();
  }

  // Do nothing in the base version
  virtual void setup() {}

  void reset() {
    setID_ = 0;
    expiredID_ = 0;
    cancelledID_ = 0;
    expired_ = false;
    cancelled_ = false;
    start_ = std::chrono::steady_clock::now();
  }

  HHWheelTimer::ID setID_;
  HHWheelTimer::ID expiredID_;
  HHWheelTimer::ID cancelledID_;

  bool expired_;
  bool cancelled_;

  std::chrono::steady_clock::time_point start_;
  std::chrono::steady_clock::time_point end_;
};

class CallbackTestWithMutex :
    public CallbackTest,
    public std::enable_shared_from_this<CallbackTestWithMutex> {
public:
  CallbackTestWithMutex(HHWheelTimer* timer) : timer_(timer) {}

  void timeoutExpired(HHWheelTimer::ID id) noexcept override {
    CallbackTest::timeoutExpired(id);
    checkDeadlock();
  }

  void timeoutCancelled(HHWheelTimer::ID id) noexcept override {
    CallbackTest::timeoutCancelled(id);
    checkDeadlock();
  }

  void setup() override {
    std::lock_guard<std::timed_mutex> guard(mutex_);
    timer_->cancelTimeout(shared_from_this());
  }

private:
  static std::timed_mutex mutex_;
  HHWheelTimer* timer_;

  void checkDeadlock() {
    std::unique_lock<std::timed_mutex> lock(mutex_, std::chrono::seconds(2));
    if (!lock) {
      throw std::runtime_error("Deadlock!!!");
    }

    lock.unlock();
  }
};

std::timed_mutex CallbackTestWithMutex::mutex_;

static void waitForUseCount(std::shared_ptr<CallbackTest>& ptr, long count)
{
  // Wait up to 2 seconds for the use_count to drop to the expected count
  for (uint32_t ii = 0; ii < 2000 && ptr.use_count() != count; ii++) {
    std::this_thread::sleep_for(msecs(1));
  }
}

class HHWheelTimerTest : public ::testing::Test {
protected:
  static void SetUpTestCase() {
    my_timer_initialize();
    slippage_ = 20;
#if defined(UNIV_DEBUG_VALGRIND)
    if (RUNNING_ON_VALGRIND) {
      slippage_ = 100;
    }
#endif
  }

  static void TearDownTestCase() {
    my_timer_deinitialize();
  }

  void validateTimeout(HHWheelTimer& timer, msecs to) {
    // Create a default callback
    auto cb = std::make_shared<CallbackTest>();
    cb->setID_ = timer.scheduleTimeout(cb, to);

    auto tm = cb->wait(to);

    // The callback should have expired, not been cancelled
    EXPECT_TRUE(cb->expired_);
    EXPECT_FALSE(cb->cancelled_);

    // The amount of time it took should match the input timeout
    EXPECT_GE(tm.count(), to.count());
    EXPECT_LE(tm.count(), to.count() + slippage_);

    // The ID that was returned by the expired function should match
    // what was set
    EXPECT_EQ(cb->setID_, cb->expiredID_);

    // Give the wheel a chance to release ownership of the shared pointer
    waitForUseCount(cb, 1);
    // Make sure the timer doesn't keep a reference to the callback
    EXPECT_EQ(cb.use_count(), 1);
  }
protected:
  static uint32_t slippage_;
};

uint32_t HHWheelTimerTest::slippage_;

#define NUM_THREADS 200
enum ControlState {
  INITIALIZING,
  RUNNING,
  STOPPING,
};

class ThreadTestThread : public thread::Thread
{
public:
  ThreadTestThread() :
      timer_{nullptr},
      state_{nullptr},
      failures_{0},
      ready_{false},
      useMutex_{false} {
  }
  virtual ~ThreadTestThread() {}

  void requireMutex() { useMutex_ = true; }
  void setTimer(HHWheelTimer* timer) { timer_ = timer; }
  void setState(enum ControlState* state) { state_ = state; }
  void run() {
    DBUG_ASSERT(timer_ != nullptr);
    DBUG_ASSERT(state_ != nullptr);

    rng_.seed(thread_id());

    // Wait for the signal to start
    ready_ = true;
    while (*state_ < RUNNING) {
      std::this_thread::sleep_for(msecs(1));
    }

    // Run our tests until we are told to stop
    while (*state_ == RUNNING) {
      try_test();
    }

    // Just exit (we can be joined then)
  }

  void try_test() {
    // Get a timeout from 10 to 1000 millseconds (in multiples of 10ms)
    auto to = msecs(((rng_() % 100) + 1) * 10);
    auto cb = useMutex_ ?
        std::dynamic_pointer_cast<CallbackTest>(
            std::make_shared<CallbackTestWithMutex>(timer_)) :
        std::make_shared<CallbackTest>();
    cb->setup();
    cb->setID_ = timer_->scheduleTimeout(cb, to);

    if (rng_() % 5 == 0) {
      // 20% of the time cancel the callback before the timeout
      auto cancel_to = msecs(rng_() % (to.count() - 5));
      DBUG_ASSERT(cancel_to.count() < to.count());
      std::this_thread::sleep_for(cancel_to);
      bool cancelled = timer_->cancelTimeout(cb);
      // It might have expired before we could successfully cancel it
      if (cancelled) {
        if (cb->expired_ || !cb->cancelled_ || cb->setID_ != cb->cancelledID_) {
          failures_++;
        }

        return;
      }
    }

    // Normal signalling of the callback
    auto tm = cb->wait(to);

    if (!cb->expired_ || cb->cancelled_ || cb->setID_ != cb->expiredID_) {
      failures_++;
    }
  }

  uint32_t num_failures() const { return failures_; }
  bool ready() const { return ready_; }

private:
  HHWheelTimer* timer_;
  enum ControlState *state_;
  std::mt19937_64 rng_;
  uint32_t failures_;
  bool ready_;
  bool useMutex_;
};

// Make sure the template parameters are set correctly.  The template
// parameters are the the tick interval [default 10ms] and default timeout
// (in ticks) [default 1]
TEST_F(HHWheelTimerTest, ClassParameters)
{
  // Default is 1 millisecond 'tick' or interval
  HHWheelTimer timer_default;
  EXPECT_EQ(timer_default.getTickInterval(), msecs(1));
  EXPECT_EQ(timer_default.getDefaultTimeout(), msecs(10));

  // Use a 10 millisecond tick directly
  HHWheelTimer timer_10ms(msecs(10));
  EXPECT_EQ(timer_10ms.getTickInterval(), msecs(10));
  EXPECT_EQ(timer_10ms.getDefaultTimeout(), msecs(10));

  // Create a timer with a two millisecond  'tick'
  HHWheelTimer timer_1ms(msecs(2));
  EXPECT_EQ(timer_1ms.getTickInterval(), msecs(2));
  EXPECT_EQ(timer_1ms.getDefaultTimeout(), msecs(10));

  // Try a one second timer
  HHWheelTimer timer_1s(msecs(1000));
  EXPECT_EQ(timer_1s.getTickInterval(), msecs(1000));
  // default was rounded up
  EXPECT_EQ(timer_1s.getDefaultTimeout(), msecs(1000));

  // Try a 73 ms timer
  HHWheelTimer timer_77ms(msecs(77));
  EXPECT_EQ(timer_77ms.getTickInterval(), msecs(77));
  // default was rounded up
  EXPECT_EQ(timer_77ms.getDefaultTimeout(), msecs(77));

  // Now try a 10ms tick and a 100ms default timeout
  HHWheelTimer timer_10ms_100ms(msecs(5), msecs(100));
  EXPECT_EQ(timer_10ms_100ms.getTickInterval(), msecs(5));
  EXPECT_EQ(timer_10ms_100ms.getDefaultTimeout(), msecs(100));

  // How about a 1ms tick and a 37ms default timeout
  HHWheelTimer timer_1ms_37ms(msecs(1), msecs(37));
  EXPECT_EQ(timer_1ms_37ms.getTickInterval(), msecs(1));
  EXPECT_EQ(timer_1ms_37ms.getDefaultTimeout(), msecs(37));
}

// Make sure our timer times out correctly
TEST_F(HHWheelTimerTest, SimpleTimeout)
{
  HHWheelTimer timer(msecs(10));

  // Try the default timeout (10ms)
  validateTimeout(timer, msecs(10));

  // Try a 100ms timeout
  validateTimeout(timer, msecs(100));

  // A timer wheel has 256 slots in each wheel.  Try a timeout that can't go
  // into the lowest wheel
  validateTimeout(timer, msecs(3000));

  // Try 0 milliseconds
  validateTimeout(timer, msecs(0));

  // Try non-multiples of the interval
  validateTimeout(timer, msecs(1));
  validateTimeout(timer, msecs(4));
  validateTimeout(timer, msecs(19));

  // This timeout takes 70 seconds
  // The purpose is to make sure a single timer cascades from the third
  // wheel down to the second and then down to the first and eventually
  // times out.  For this to happen the timeout has to be large enough.
  // Since each wheel holds 256 entries, any timeout up to 65535ms will
  // fit into the first two wheels.  Bump the timeout up to 70 seconds
  // to make sure of getting into the third wheel.
  HHWheelTimer timer2(msecs(1));
  validateTimeout(timer2, msecs(70000));
}

// Make sure multiple timers all timeout correctly
TEST_F(HHWheelTimerTest, ComplexTimeout)
{
  HHWheelTimer timer;

  auto cb_long = std::make_shared<CallbackTest>();
  auto cb_med = std::make_shared<CallbackTest>();
  auto cb_short = std::make_shared<CallbackTest>();
  auto cb_tiny = std::make_shared<CallbackTest>();

  cb_long->setID_ = timer.scheduleTimeout(cb_long, msecs(10000));
  for (uint32_t ii = 0; ii < 10; ii++) {
    cb_med->reset();
    cb_med->setID_ = timer.scheduleTimeout(cb_med, msecs(1000));
    for (uint32_t jj = 0; jj < 10; jj++) {
      cb_short->reset();
      cb_short->setID_ = timer.scheduleTimeout(cb_short, msecs(100));
      for (uint32_t kk = 0; kk < 10; kk++) {
        cb_tiny->reset();
        cb_tiny->setID_ = timer.scheduleTimeout(cb_tiny, msecs(10));
        auto to = cb_tiny->wait(msecs(10));
        EXPECT_TRUE(cb_tiny->expired_);
        EXPECT_FALSE(cb_tiny->cancelled_);
        EXPECT_EQ(cb_tiny->setID_, cb_tiny->expiredID_);
        EXPECT_GE(to.count(), 10);
        EXPECT_LE(to.count(), 10 + slippage_);
      }

      auto to = cb_short->wait(msecs(100));
      EXPECT_TRUE(cb_short->expired_);
      EXPECT_FALSE(cb_short->cancelled_);
      EXPECT_EQ(cb_short->setID_, cb_short->expiredID_);
      EXPECT_GE(to.count(), 100);
      EXPECT_LE(to.count(), 100 + slippage_);
    }

    auto to = cb_med->wait(msecs(1000));
    EXPECT_TRUE(cb_med->expired_);
    EXPECT_FALSE(cb_med->cancelled_);
    EXPECT_EQ(cb_med->setID_, cb_med->expiredID_);
    EXPECT_GE(to.count(), 1000);
    EXPECT_LT(to.count(), 1000 + slippage_);
  }

  auto to = cb_long->wait(msecs(10000));
  EXPECT_TRUE(cb_long->expired_);
  EXPECT_FALSE(cb_long->cancelled_);
  EXPECT_EQ(cb_long->setID_, cb_long->expiredID_);
  EXPECT_GE(to.count(), 10000);
  EXPECT_LT(to.count(), 10000 + slippage_);
}

// Check timer cancelling
TEST_F(HHWheelTimerTest, Cancel)
{
  HHWheelTimer timer;

  // Create a default callback
  auto cb = std::make_shared<CallbackTest>();
  cb->setID_ = timer.scheduleTimeout(cb);

  EXPECT_TRUE(timer.cancelTimeout(cb));

  // The callback should have cancelled, not expired
  EXPECT_FALSE(cb->expired_);
  EXPECT_TRUE(cb->cancelled_);

  // The ID that was returned by the cancelled function should match
  // what was set
  EXPECT_EQ(cb->setID_, cb->cancelledID_);

  // Give the wheel a chance to release ownership of the shared pointer
  waitForUseCount(cb, 1);
  // Make sure the timer doesn't keep a reference to the callback
  EXPECT_EQ(cb.use_count(), 1);
}

// Check that the HHWheelTimer destructor cancels all timers
TEST_F(HHWheelTimerTest, Destructor)
{
  auto cb1 = std::make_shared<CallbackTest>();
  auto cb2 = std::make_shared<CallbackTest>();
  {
    HHWheelTimer timer;

    timer.scheduleTimeout(cb1);
    timer.scheduleTimeout(cb2, msecs(100));
  }

  // When timer went out of scope it should have cancelled both timers
  EXPECT_FALSE(cb1->expired_);
  EXPECT_TRUE(cb1->cancelled_);
  EXPECT_FALSE(cb2->expired_);
  EXPECT_TRUE(cb2->cancelled_);
}

// Reset time before it triggers
TEST_F(HHWheelTimerTest, Reset)
{
  HHWheelTimer timer;

  // Create a default callback
  auto cb = std::make_shared<CallbackTest>();

  // Every 5 ms set the timeout 10 ms ahead
  for (uint32_t ii = 0; ii < 200; ii++) {
    timer.scheduleTimeout(cb);
    std::this_thread::sleep_for(msecs(5));
  }

  // Finally cancel the timeout
  EXPECT_TRUE(timer.cancelTimeout(cb));

  // The callback should never have expired, but it should have been cancelled
  EXPECT_FALSE(cb->expired_);
  EXPECT_TRUE(cb->cancelled_);

  // We should have slept at least 1000ms and less than 2000ms
  EXPECT_TRUE(cb->duration() > msecs(1000));
  EXPECT_TRUE(cb->duration() < msecs(2000));
}

// Multiple timers
TEST_F(HHWheelTimerTest, MultipleTimers)
{
  HHWheelTimer timer1;
  HHWheelTimer timer2;

  auto cb1 = std::make_shared<CallbackTest>();
  auto cb2 = std::make_shared<CallbackTest>();

  cb1->setID_ = timer1.scheduleTimeout(cb1);
  cb2->setID_ = timer2.scheduleTimeout(cb2);

  cb1->wait(timer1.getDefaultTimeout());
  cb2->wait(timer2.getDefaultTimeout());

  EXPECT_TRUE(cb1->expired_);
  EXPECT_FALSE(cb1->cancelled_);
  EXPECT_EQ(cb1->setID_, cb1->expiredID_);

  EXPECT_TRUE(cb2->expired_);
  EXPECT_FALSE(cb2->cancelled_);
  EXPECT_EQ(cb2->setID_, cb2->expiredID_);
}

// Reuse a callback after an expiration or cancallation
TEST_F(HHWheelTimerTest, Reuse)
{
  HHWheelTimer timer1;
  HHWheelTimer timer2;

  // Create a callback and use it on the first timer
  auto cb = std::make_shared<CallbackTest>();

  cb->setID_ = timer1.scheduleTimeout(cb);
  auto tm = cb->wait(timer1.getDefaultTimeout());

  EXPECT_TRUE(cb->expired_);
  EXPECT_FALSE(cb->cancelled_);
  EXPECT_EQ(cb->setID_, cb->expiredID_);
  EXPECT_GE(tm, msecs(10));
  EXPECT_LT(tm, msecs(10 + slippage_));

  // Now reset and use the callback on the second timer
  cb->reset();
  cb->setID_ = timer2.scheduleTimeout(cb);
  tm = cb->wait(timer2.getDefaultTimeout());

  EXPECT_TRUE(cb->expired_);
  EXPECT_FALSE(cb->cancelled_);
  EXPECT_EQ(cb->setID_, cb->expiredID_);
  EXPECT_GE(tm, msecs(10));
  EXPECT_LT(tm, msecs(10 + slippage_));

  // Reset it again and use it on the first timer with a cancellation
  cb->reset();
  cb->setID_ = timer1.scheduleTimeout(cb);
  std::this_thread::sleep_for(msecs(5));
  EXPECT_TRUE(timer1.cancelTimeout(cb));

  EXPECT_FALSE(cb->expired_);
  EXPECT_TRUE(cb->cancelled_);
  EXPECT_EQ(cb->setID_, cb->cancelledID_);
}

// Have the callback go out of scope
TEST_F(HHWheelTimerTest, CallbackOutOfScope)
{
  HHWheelTimer timer;

  {
    auto cb = std::make_shared<CallbackTest>();
    timer.scheduleTimeout(cb, msecs(100));
  }

  // Make sure the count drops to zero
  EXPECT_EQ(timer.count(), 1U);
  while (timer.count() != 0) {
    std::this_thread::sleep_for(msecs(1));
  }

  EXPECT_EQ(timer.count(), 0U);
}

// Run many threads all doing timeouts
TEST_F(HHWheelTimerTest, Threads)
{
  HHWheelTimer timer;
  ThreadTestThread threads[NUM_THREADS];
  enum ControlState state = INITIALIZING;

  // Get each thread up and running and pass in a pointer to the timer
  for (uint32_t ii = 0; ii < NUM_THREADS; ii++) {
    threads[ii].setTimer(&timer);
    threads[ii].setState(&state);
    threads[ii].start();
  }

  // Make sure each thread is ready to run
  for (uint32_t ii = 0; ii < NUM_THREADS; ii++) {
    while (!threads[ii].ready()) {
      std::this_thread::sleep_for(msecs(1));
    }
  }

  // Begin timers
  state = RUNNING;

  // Wait for 60 seconds
  auto cb = std::make_shared<CallbackTest>();
  timer.scheduleTimeout(cb, k60Seconds);
  cb->wait(k60Seconds);

  // Stop the threads
  state = STOPPING;

  for (uint32_t ii = 0; ii < NUM_THREADS; ii++) {
    threads[ii].join();
    EXPECT_EQ(threads[ii].num_failures(), 0U);
  }
}

// Run many threads all doing timeouts and using a mutex
TEST_F(HHWheelTimerTest, ThreadsWithAMutex)
{
  HHWheelTimer timer;
  ThreadTestThread threads[NUM_THREADS];
  enum ControlState state = INITIALIZING;

  // Get each thread up and running and pass in a pointer to the timer
  for (uint32_t ii = 0; ii < NUM_THREADS; ii++) {
    threads[ii].requireMutex();
    threads[ii].setTimer(&timer);
    threads[ii].setState(&state);
    threads[ii].start();
  }

  // Make sure each thread is ready to run
  for (uint32_t ii = 0; ii < NUM_THREADS; ii++) {
    while (!threads[ii].ready()) {
      std::this_thread::sleep_for(msecs(1));
    }
  }

  // Begin timers
  state = RUNNING;

  // Wait for 60 seconds
  auto cb = std::dynamic_pointer_cast<CallbackTest>(
      std::make_shared<CallbackTestWithMutex>(&timer));
  timer.scheduleTimeout(cb, k60Seconds);
  cb->wait(k60Seconds);

  // Stop the threads
  state = STOPPING;

  for (uint32_t ii = 0; ii < NUM_THREADS; ii++) {
    threads[ii].join();
    EXPECT_EQ(threads[ii].num_failures(), 0U);
  }
}

}  // namespace
