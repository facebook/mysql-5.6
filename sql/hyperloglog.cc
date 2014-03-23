#include "hyperloglog.h"
#include "my_sys.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

const uchar bit_parts[] = {16,8,4,2,1};
const uchar default_data_size_log2 = 10;
const double long_range_adjustment_constant32 = 4.294967296e9;

uchar find_first_set_bit_after_index(uint hash, uchar start_bit) {
  uint num = hash >> start_bit;
  uchar pos = 0;
  int i;
  if (num == 0) {
    return (uchar)(32 - start_bit);
  }

  for(i = 0; i < 5; i++) {
    uchar part = bit_parts[i];
    if((num & ((1 << part) - 1)) == 0) {
      num >>= part;
      pos += part;
    }
  }
  return pos;
}

// alpha_m in the hyperloglog paper. Refer to the comment in hyperloglog.h
double get_harmonic_mean_constant(uint data_size) {
  if (data_size >= 128) {
    return 0.7213 / (1.079 / data_size + 1.0);
  } else if (data_size == 16) {
    return 0.673;
  } else if (data_size == 32) {
    return 0.697;
  } else if (data_size == 64) {
    return 0.709;
  }
  //Should never reach here.
  return 0;
}

void hyperloglog_init(struct hyperloglog* hll) {
  hll->data_size_log2 = default_data_size_log2;
  hll->data_size = 1 << hll->data_size_log2;
  hll->max_bit_position = 32 - hll->data_size_log2 + 1;
  hll->data = (uint*)my_malloc(
    hll->data_size * hll->max_bit_position * sizeof(uint),
    MYF(MY_WME));
  memset(hll->data, 0, hll->data_size * hll->max_bit_position * sizeof(uint));
}

void hyperloglog_reset(struct hyperloglog* hll) {
  memset(hll->data, 0, hll->data_size * hll->max_bit_position * sizeof(uint));
}

ulonglong hyperloglog_query(struct hyperloglog* hll, uint since_time) {
  double harmonic_mean_constant = get_harmonic_mean_constant(hll->data_size);
  double query_sum = 0.0;
  uint count_zero_elements = 0;
  uint i,j;
  double cardinality_estimate = 0.0;

  for (i = 0; i < hll->data_size; i++) {
    uchar max_valid_index = 32;
    // We need the maximum index seen since since_time.
    // As data[i][j] stores the last time when an index of j was seen,
    // We just need to find the largest j, for which data[i][j] is
    // larger than since_time.
    for (j = hll->max_bit_position; j >= 1; j--) {
      if (hll->data[i * hll->max_bit_position + j - 1] > since_time) {
    max_valid_index = j;
    break;
      }
    }
    if (max_valid_index == 32) {
      count_zero_elements++;
      query_sum += 1.0;
    } else {
      query_sum += 1.0 / ((uint) 1 << max_valid_index);
    }
  }

  cardinality_estimate =
    harmonic_mean_constant * hll->data_size * hll->data_size / query_sum;

  if (cardinality_estimate <= 2.5 * hll->data_size) {
    // small range correction
    if (count_zero_elements != 0)
      cardinality_estimate = log((double)hll->data_size / count_zero_elements)
        * hll->data_size;
  } else if (cardinality_estimate > long_range_adjustment_constant32 / 30.0) {
    // Adjust for hash collisions that occur when nearing 2^32 uniques
    cardinality_estimate = -long_range_adjustment_constant32 *
      log(1.0 - cardinality_estimate / long_range_adjustment_constant32);
  }
  return (ulonglong)(cardinality_estimate + 0.5);
}

void hyperloglog_insert(
  struct hyperloglog* hll,
  uint hash,
  uint current_time) {
    uint index = hash & (hll->data_size-1);
  uint first_set_bit =
    find_first_set_bit_after_index(hash, hll->data_size_log2);
  hll->data[index * hll->max_bit_position + first_set_bit] = current_time;
}

void hyperloglog_destroy(struct hyperloglog* hll) {
  my_free((char*)(hll->data));
}
