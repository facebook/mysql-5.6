////////////////////////////////////////////////////////////////////////////////
// This is free and unencumbered software released into the public domain.

// C++ implementation of Dmitry Vyukov's non-intrusive
// lock free unbound MPSC queue
// http://www.1024cores.net/home/
// lock-free-algorithms/queues/non-intrusive-mpsc-node-based-queue

//from https://github.com/mstump/queues
//This is free and unencumbered software released into the public domain.
//
//Anyone is free to copy, modify, publish, use, compile, sell, or
//distribute this software, either in source code form or as a compiled
//binary, for any purpose, commercial or non-commercial, and by any
//means.
//
//In jurisdictions that recognize copyright laws, the author or authors
//of this software dedicate any and all copyright interest in the
//software to the public domain. We make this dedication for the benefit
//of the public at large and to the detriment of our heirs and
//successors. We intend this dedication to be an overt act of
//relinquishment in perpetuity of all present and future rights to this
//software under copyright law.
//
//THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
//EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
//IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
//OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
//ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
//OTHER DEALINGS IN THE SOFTWARE.
//
//For more information, please refer to <http://unlicense.org>

// License from http://www.1024cores.net/home/
// lock-free-algorithms/queues/non-intrusive-mpsc-node-based-queue
//Copyright (c) 2010-2011 Dmitry Vyukov. All rights reserved.
//Redistribution and use in source and binary forms, with or
//without modification, are permitted provided that the following
//conditions are met:
//1. Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
//2. Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY DMITRY VYUKOV "AS IS" AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
// OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
// EVENT SHALL DMITRY VYUKOV OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
// BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
// USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// The views and conclusions contained in the software and documentation
// are those of the authors and should not be interpreted as representing
// official policies, either expressed or implied, of Dmitry Vyukov.
//

#ifndef __MPSC_BOUNDED_QUEUE_INCLUDED__
#define __MPSC_BOUNDED_QUEUE_INCLUDED__

#include <atomic>
#include <type_traits>
#include <cassert>
#ifndef DBUG_OFF
#include "my_global.h"
#include "my_pthread.h"
#endif


/**
 * Multiple Producer Single Consumer Lockless Q
 */
template<typename T>
class mpsc_queue_t
{
public:
  struct buffer_node_t
  {
    T                           data;
    std::atomic<buffer_node_t*> next;
  };

  mpsc_queue_t() :
    _head(reinterpret_cast<buffer_node_t*>(new buffer_node_aligned_t)),
    _tail(_head.load(std::memory_order_relaxed))
#if !defined(DBUG_OFF)
    , dequeue_in_use(false)
#endif
  {
    buffer_node_t* front = _head.load(std::memory_order_relaxed);
    front->next.store(nullptr, std::memory_order_relaxed);
  }

  ~mpsc_queue_t()
  {
    T output;
    while (this->dequeue(output)) {}
    buffer_node_t* front = _head.load(std::memory_order_relaxed);
    delete front;
  }

  // multi -producer single consumer queue where enqueue is thread safe
  void enqueue(const T& input)
  {
    buffer_node_t* node =
      reinterpret_cast<buffer_node_t*>(new buffer_node_aligned_t);
    node->data = input;
    node->next.store(nullptr, std::memory_order_relaxed);

    buffer_node_t* prev_head = _head.exchange(node, std::memory_order_acq_rel);
    prev_head->next.store(node, std::memory_order_release);
  }

  // only 1 thread should dequeu this queue. We break the assumption only in
  // the destructor where we expect all consumers have flushed out
  // hence the assert.
  bool dequeue(T& output)
  {
    // Make sure it was false before
    DBUG_ASSERT(!dequeue_in_use.exchange(true));

    buffer_node_t* tail = _tail.load(std::memory_order_relaxed);
    buffer_node_t* next = tail->next.load(std::memory_order_acquire);

    bool result = false;
    if (next != nullptr) {
      output = next->data;
      _tail.store(next, std::memory_order_release);
      delete tail;
      result = true;
    }

    DBUG_ASSERT(dequeue_in_use.exchange(false)); // Make sure it was true before
    return result;
  }

private:

  typedef typename std::aligned_storage<sizeof(buffer_node_t),
    std::alignment_of<buffer_node_t>::value>::type buffer_node_aligned_t;

  std::atomic<buffer_node_t*> _head;
  std::atomic<buffer_node_t*> _tail;

  mpsc_queue_t(const mpsc_queue_t&)=delete;
  void operator=(const mpsc_queue_t&)=delete;

#if !defined(DBUG_OFF)
  std::atomic<bool> dequeue_in_use;
#endif
};

#endif
