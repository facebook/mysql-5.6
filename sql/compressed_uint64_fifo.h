/*
  Copyright (C) 2024, VilniusDB

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#ifndef COMPRESSED_UINT64_FIFO_
#define COMPRESSED_UINT64_FIFO_

#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>

// A space-efficient monotonic sequence of uint64_t numbers. The expectation is
// that the majority of deltas between consecutive numbers are equal to one. The
// numbers are both inserted and read one-by-one with no temporal locality (i.e.
// the implementation should not be SIMD'ized).
class [[nodiscard]] compressed_uint64_fifo final {
  // The data is compressed to a stream of unsigned bytes.
  using stream_type = std::vector<std::uint8_t>;

  // The highest two bits in each byte determine the interpretation:
  // - the highest bit set: this number is a count of successive deltas
  //   equal to one in the stream of numbers. Otherwise this number is a delta
  //   to add to the previous returned number in the sequence. If a number
  //   requires multiple bytes to encode it, this bit will be set or cleared
  //   in all the bytes.
  static constexpr std::uint8_t RUN_OF_ONES_MASK = 0x80U;

  // - the second-highest bit set: the next byte will
  //   provide the next higher 6 bits of the current number. This
  //   encoding is similar to unsigned LEB128, except that six instead of seven
  //   bits are used per byte.
  static constexpr std::uint8_t NUM_CONTINUES_MASK = 0x40U;

  // The lowest six bits are value data
  static constexpr auto DATA_PER_BYTE_LEN = 6;
  static constexpr std::uint8_t NUM_DATA_MASK = 0x3FU;

  stream_type data;
  std::uint64_t last_number{0};
  std::uint64_t current_run_of_ones_len{0};

  void write(std::uint64_t number, bool is_run_of_ones_length) {
    assert(number != 0);

    while (number > 0) {
      auto next_lowest_byte = (number & NUM_DATA_MASK) |
                              (is_run_of_ones_length ? RUN_OF_ONES_MASK : 0U);
      number >>= DATA_PER_BYTE_LEN;
      if (number != 0) next_lowest_byte |= NUM_CONTINUES_MASK;
      data.push_back(next_lowest_byte);
    }
  }

 public:
  class const_iterator {
    const compressed_uint64_fifo &container;
    stream_type::const_iterator stream_itr;
    std::uint64_t current_number{0};
    std::uint64_t used_last_run_of_ones_len;
    std::uint64_t remaining_run_of_ones_len{0};

    std::pair<std::uint64_t, bool> read() noexcept {
      bool is_run_of_ones_length = false;
      bool continues = false;
      std::uint64_t number = 0U;
      unsigned next_byte_pos = 0U;
      do {
        std::uint64_t next_lowest_byte = *stream_itr++;
        assert(!!(next_lowest_byte & RUN_OF_ONES_MASK) ==
                   is_run_of_ones_length ||
               number == 0U);
        if (!continues) {
          is_run_of_ones_length = next_lowest_byte & RUN_OF_ONES_MASK;
        } else {
          assert(is_run_of_ones_length ==
                 !!(next_lowest_byte & RUN_OF_ONES_MASK));
        }
        continues = next_lowest_byte & NUM_CONTINUES_MASK;
        assert(!continues || stream_itr != container.data.end());
        next_lowest_byte &= NUM_DATA_MASK;
        number |= (next_lowest_byte << next_byte_pos);
        next_byte_pos += DATA_PER_BYTE_LEN;
      } while (continues);
      assert(number > 0);
      return std::make_pair(number, is_run_of_ones_length);
    }

    void advance() noexcept {
      if (stream_itr == container.data.end()) {
        // Any runs-of-one lengths in container are always followed by deltas,
        // thus if we are at the end, we just processed a delta.
        assert(remaining_run_of_ones_len == 0);
        if (used_last_run_of_ones_len == container.current_run_of_ones_len) {
          assert(current_number == container.last_number);
          current_number = 0;
          return;
        }
        ++used_last_run_of_ones_len;
        ++current_number;
        return;
      }
      if (remaining_run_of_ones_len > 0) {
        ++current_number;
        --remaining_run_of_ones_len;
        return;
      }
      const auto [number, is_run_of_ones_length] = read();
      if (is_run_of_ones_length) {
        // Must be followed by a delta, thus can't be the end
        assert(stream_itr != container.data.end());
        ++current_number;
        remaining_run_of_ones_len = number - 1;
        return;
      }
      assert(remaining_run_of_ones_len == 0);
      current_number += number;
    }

    friend class compressed_uint64_fifo;

    // Return iterator that points at the end
    const_iterator(const compressed_uint64_fifo &container_, bool) noexcept
        : container{container_},
          stream_itr{container.data.cend()},
          used_last_run_of_ones_len{container.current_run_of_ones_len} {}

    explicit const_iterator(const compressed_uint64_fifo &container_) noexcept
        : container{container_},
          stream_itr{container.data.cbegin()},
          used_last_run_of_ones_len{0} {
      advance();
    }

   public:
    [[nodiscard]] std::uint64_t operator*() const noexcept {
      return current_number;
    }

    [[nodiscard]] const_iterator &operator++() noexcept {
      advance();
      return *this;
    }

    [[nodiscard]] bool operator==(const const_iterator &other) const noexcept {
      assert(&container == &other.container);
      return current_number == other.current_number;
    }

    [[nodiscard]] bool operator!=(const const_iterator &other) const noexcept {
      assert(&container == &other.container);
      return current_number != other.current_number;
    }
  };

  [[nodiscard]] bool empty() const noexcept {
    return data.empty() && current_run_of_ones_len == 0;
  }

  [[nodiscard]] std::uint64_t back() const noexcept {
    assert(!empty());
    return last_number;
  }

  void clear() {
    data.clear();
    last_number = 0;
    current_run_of_ones_len = 0;
  }

  void push_back(std::uint64_t value) {
    // MyISAM supporting auto_increment on key parts means that values may go
    // down as well, breaking the monotonicity. Support this by allowing large
    // deltas that wrap around. The compression will be poor but MyISAM is not
    // used much (or at all).
    // Rethink the approach if MyRocks starts supporting composite primary keys
    // with auto_increment key suffixes. Otherwise replace this assert with a
    // ">" one once MyISAM is killed.
    assert(value != last_number);

    const auto delta = value - last_number;
    if (delta == 1) {
      ++current_run_of_ones_len;
    } else {
      data.reserve(128);
      if (current_run_of_ones_len > 0) {
        write(current_run_of_ones_len, true);
        current_run_of_ones_len = 0;
      }
      write(delta, false);
    }
    last_number = value;
  }

  [[nodiscard]] const_iterator begin() const noexcept {
    return const_iterator{*this};
  }

  [[nodiscard]] const_iterator end() const noexcept {
    return const_iterator{*this, true};
  }

  compressed_uint64_fifo() = default;
  ~compressed_uint64_fifo() = default;
  compressed_uint64_fifo(compressed_uint64_fifo &&) = default;
  compressed_uint64_fifo &operator=(compressed_uint64_fifo &&) = default;
  // Nothing wrong with copying but the current users do not need it, thus catch
  // inadvertent calls
  compressed_uint64_fifo(const compressed_uint64_fifo &) = delete;
  compressed_uint64_fifo &operator=(const compressed_uint64_fifo &) = delete;
};

#endif
