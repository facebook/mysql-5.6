#include <array>

#include "sql_digest.h"
#include "my_murmur3.h"

#define MD5_BUFF_LENGTH 32

/*
  md5_key is used as the hash key into the SQL_STATISTICS and related tables.

  It needs a hash function for usage in std::unordered_map since the standard
  library doesn't provide a specialization for std::array<>. Just use murmur3
  from mysql.
*/
using md5_key = std::array<unsigned char, MD5_HASH_SIZE>;

namespace std {
  template <>
  struct hash<md5_key>
  {
    std::size_t operator()(const md5_key& k) const
    {
      return murmur3_32(k.data(), k.size(), 0);
    }
  };
}
