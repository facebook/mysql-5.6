#ifndef THREAD_ITERATOR_H
#define THREAD_ITERATOR_H

#include <set>
#include <vector>

class THD;

#ifdef SHARDED_LOCKING
class ShardedThreads;

/**
 * This is a iterator class which iterates over all THD's,
 * when the THD's are stored in multiple set's instead of
 * a single set. This approach is akin to sharding.
 */
class Thread_iterator {

private:
   ShardedThreads *m_sharded_threads;
   uint m_setno;
   std::set<THD*>::iterator m_setptr;

public:

   Thread_iterator(ShardedThreads *st, uint sno, std::set<THD*>::iterator sptr);
   void operator++();
   THD* operator*();
   Thread_iterator& operator=(const Thread_iterator& other);
   bool operator==(const Thread_iterator& other) const;
   bool operator!=(const Thread_iterator& other) const;

};

/**
 * Instead of a single global set of threads, we use
 * multiple sets/shards to reduce contention.
 */
class ShardedThreads {

  friend class Thread_iterator;
 protected:

   std::vector<std::set<THD*> > m_thread_list;
   uint m_size;
 public:

   typedef std::set<THD*>::iterator t_setitr;

   std::pair<t_setitr, bool> insert(THD*& value);

   size_t erase(THD*& value);

   ShardedThreads(uint sz) : m_size(sz) {
     m_thread_list.resize(m_size);
   }

   ~ShardedThreads() { }

   Thread_iterator find(THD *thd);

   Thread_iterator begin() {
      return Thread_iterator(this, 0, m_thread_list[0].begin());
   }

   Thread_iterator end() {
     return Thread_iterator(this, m_size - 1, m_thread_list[m_size-1].end());
   }
};
#else
typedef std::set<THD*>::iterator Thread_iterator;
#endif

#endif
