#include "my_global.h"
#include "global_threads.h"

#ifdef SHARDED_LOCKING
// currently this is global, but we
// can make it per lock type if lock sharding
// as a strategy takes off.
my_bool gl_lock_sharding= 0;
uint num_sharded_locks= 4;

// Thomas Wang integer hash function
//http://web.archive.org/web/20071223173210
// /http://www.concentric.net/~Ttwang/tech/inthash.htm
// Used in open source community for long time
static uint32_t hash6432shift(uint64_t key)
{
  key = (~key) + (key << 18); // key = (key << 18) - key - 1;
  key = key ^ (key >> 31);
  key = key * 21; // key = (key + (key << 2)) + (key << 4);
  key = key ^ (key >> 11);
  key = key + (key << 6);
  key = key ^ (key >> 22);
  return (int) key;
}

static size_t get_mutex_shard(const THD *thd) {
  uint32_t hv = hash6432shift((uint64_t)thd);
  return hv % num_sharded_locks;
}

std::pair<ShardedThreads::t_setitr, bool> ShardedThreads::insert(THD*& value ) {
  return m_thread_list[get_mutex_shard(value)].insert(value);
}

size_t ShardedThreads::erase(THD*& value) {
  return m_thread_list[get_mutex_shard(value)].erase(value);
}

Thread_iterator ShardedThreads::find(THD *value) {
   uint sv = get_mutex_shard(value);
   auto itr = m_thread_list[sv].find(value);
   if (itr != m_thread_list[sv].end()) {
     return Thread_iterator(this, sv, itr);
   } else
     return end();
}

Thread_iterator::Thread_iterator(ShardedThreads *st,
  uint sn, std::set<THD*>::iterator sptr)
: m_sharded_threads(st), m_setno(sn), m_setptr(sptr) {
  if (m_setno == 0 && m_setptr == m_sharded_threads->m_thread_list[0].begin()) {
    while (m_setno < (m_sharded_threads->m_size-1 ) &&
      m_setptr == m_sharded_threads->m_thread_list[m_setno].end()) {
      m_setno++;
      m_setptr = m_sharded_threads->m_thread_list[m_setno].begin();
    }
  }
}

Thread_iterator& Thread_iterator::operator=(const Thread_iterator& other) {
  if (this != &other) {
    m_sharded_threads = other.m_sharded_threads;
    m_setno = other.m_setno;
    m_setptr = other.m_setptr;
  }
  return *this;
}

bool Thread_iterator::operator==(const Thread_iterator& other) const {
  return m_sharded_threads == other.m_sharded_threads &&
    m_setno == other.m_setno && m_setptr == other.m_setptr;
}

bool Thread_iterator::operator!=(const Thread_iterator& other) const {
  return !(*this == other);
}

void Thread_iterator::operator++() {
  ++m_setptr;
  while (m_setno < (m_sharded_threads->m_size-1 ) &&
    m_setptr == m_sharded_threads->m_thread_list[m_setno].end()) {
    m_setno++;
    m_setptr = m_sharded_threads->m_thread_list[m_setno].begin();
  }
}

THD* Thread_iterator::operator*() {
  return *m_setptr;
}

void mutex_assert_owner_all_shards(mysql_mutex_t *mtx,
  std::vector<mysql_mutex_t> *mtx_array) {
#if !defined(DBUG_OFF)
  if (gl_lock_sharding) {
    for (auto& mtx : *mtx_array) {
      mysql_mutex_assert_owner(&mtx);
    }
  } else
    mysql_mutex_assert_owner(mtx);
#endif
}

void mutex_assert_not_owner_all_shards(mysql_mutex_t *mtx,
  std::vector<mysql_mutex_t> *mtx_array) {
#if !defined(DBUG_OFF)
  if (gl_lock_sharding) {
    for (auto& mtx : *mtx_array) {
      mysql_mutex_assert_not_owner(&mtx);
    }
  } else
    mysql_mutex_assert_not_owner(mtx);
#endif
}

void mutex_assert_owner_shard(mysql_mutex_t *mtx,
  std::vector<mysql_mutex_t> *mtx_array, const THD *thd) {
  if (gl_lock_sharding)
    mysql_mutex_assert_owner(&(*mtx_array)[get_mutex_shard(thd)]);
  else
    mysql_mutex_assert_owner(mtx);
}

void mutex_assert_not_owner_shard(mysql_mutex_t *mtx,
  std::vector<mysql_mutex_t> *mtx_array, const THD *thd) {
  if (gl_lock_sharding)
    mysql_mutex_assert_not_owner(&(*mtx_array)[get_mutex_shard(thd)]);
  else
    mysql_mutex_assert_not_owner(mtx);
}

void mutex_lock_all_shards(mysql_mutex_t *mtx,
  std::vector<mysql_mutex_t> *mtx_array) {
  if (gl_lock_sharding) {
    for (auto& mtx : *mtx_array) {
      mysql_mutex_lock(&mtx);
    }
  } else
    mysql_mutex_lock(mtx);
}

void mutex_unlock_all_shards(mysql_mutex_t *mtx,
  std::vector<mysql_mutex_t> *mtx_array) {
  if (gl_lock_sharding) {
    for (auto itr = mtx_array->rbegin(); itr != mtx_array->rend(); ++itr) {
      mysql_mutex_unlock(&(*itr));
    }
  } else
    mysql_mutex_unlock(mtx);
}

void mutex_lock_shard(mysql_mutex_t *mtx,
  std::vector<mysql_mutex_t> *mtx_array, const THD *thd) {
  if (gl_lock_sharding)
    mysql_mutex_lock(&(*mtx_array)[get_mutex_shard(thd)]);
  else
    mysql_mutex_lock(mtx);
}

void mutex_unlock_shard(mysql_mutex_t *mtx,
  std::vector<mysql_mutex_t> *mtx_array, const THD *thd) {
  if (gl_lock_sharding)
    mysql_mutex_unlock(&(*mtx_array)[get_mutex_shard(thd)]);
  else
    mysql_mutex_unlock(mtx);
}
#endif
