// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include <cstdint>

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace utils {

enum PerfCounterMode {
  PCM_THREAD = 1,
  PCM_PROCESS = 2,
};

enum PerfCounterType {
  PCT_INSTRUCTIONS = 1,
  PCT_CPU_CYCLES = 2,
};

class PerfCounter {
 public:
  virtual ~PerfCounter() {}

  // call start() in order to start counter
  virtual void start() = 0;

  // call stop() in order to stop counter
  virtual void stop() = 0;

  // get() will return the accumulated counter value
  // since start() or getAndRestart() call.
  // get() will not reset the counter,
  // so consecutive get() calls will return increasing values
  virtual uint64_t get() = 0;

  // similar to get() but getAndRestart() resets the counter.
  virtual uint64_t getAndRestart() = 0;

  // similar to get() but getAndStop() stops the counter.
  virtual uint64_t getAndStop() = 0;

  // returns the multiplex factor
  virtual float getMultiplexFactor() = 0;
};


class PerfCounterFactory {

public:
  static std::shared_ptr<PerfCounterFactory>
    getFactory(const std::string& factory_name);

  virtual std::shared_ptr<PerfCounter> makeSharedPerfCounter(
    PerfCounterMode mode, PerfCounterType c_type) = 0;

  virtual std::unique_ptr<PerfCounter> makePerfCounter(
    PerfCounterMode mode, PerfCounterType c_type) = 0;
};

}
