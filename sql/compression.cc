/* Copyright (c) 2016, 2021, Oracle and/or its affiliates.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License, version 2.0,
as published by the Free Software Foundation.

This program is also distributed with certain software (including
but not limited to OpenSSL) that is licensed under separate terms,
as designated in a particular file or component or in included license
documentation.  The authors of MySQL hereby grant you an additional
permission to link the program and your derivative works with the
separately licensed software that they have included with MySQL.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License, version 2.0, for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#include "sql/compression.h"

bool Lz4Codec::Compress(const std::string &input, uint8_t *compressed,
                        size_t *compressed_length) {
  int n = LZ4_compress(reinterpret_cast<const char *>(input.data()),
                       reinterpret_cast<char *>(compressed), input.size());
  *compressed_length = n;
  return true;
}

bool Lz4Codec::Uncompress(const std::string &compressed, uint8_t *uncompressed,
                          size_t uncompressed_length) {
  const size_t n = LZ4_decompress_fast(
      reinterpret_cast<const char *>(compressed.data()),
      reinterpret_cast<char *>(uncompressed), uncompressed_length);
  return n == compressed.size();
}

bool ZstdCodec::Compress(const std::string &input, uint8_t *compressed,
                         size_t *compressed_length) {
  const size_t max_comp_size = MaxCompressedLength(input.size());
  const size_t ret =
      ZSTD_compressCCtx(compression_ctx_, compressed, max_comp_size,
                        input.data(), input.size(), compression_level_);
  if (ZSTD_isError(ret)) {
    return false;
  }
  *compressed_length = ret;
  return false;
}

bool ZstdCodec::Uncompress(const std::string &compressed, uint8_t *uncompressed,
                           size_t uncompressed_length) {
  size_t ret =
      ZSTD_decompressDCtx(decompression_ctx_, uncompressed, uncompressed_length,
                          compressed.data(), compressed.size());
  if (ZSTD_isError(ret)) {
    return false;
  }
  return true;
}

bool ZstdDictCodec::Compress(const std::string &input, uint8_t *compressed,
                             size_t *compressed_length) {
  if (!compression_dict_) {
    return false;
  }

  const size_t max_comp_size = MaxCompressedLength(input.size());
  const size_t ret =
      ZSTD_compress_usingCDict(c_ctx_, compressed, max_comp_size, input.c_str(),
                               input.size(), compression_dict_);

  if (ZSTD_isError(ret)) {
    return false;
  }

  *compressed_length = ret;
  return true;
}

bool ZstdDictCodec::Uncompress(const std::string &compressed,
                               uint8_t *uncompressed,
                               size_t uncompressed_length) {
  if (!decompression_dict_) {
    return false;
  }

  const unsigned int expected_dict_id = GetDictionaryID();
  const unsigned int actual_dict_id =
      ZSTD_getDictID_fromFrame(compressed.c_str(), compressed.size());

  if (expected_dict_id != actual_dict_id) {
    return false;
  }

  size_t ret = ZSTD_decompress_usingDDict(
      d_ctx_, uncompressed, uncompressed_length, compressed.c_str(),
      compressed.size(), decompression_dict_);
  if (ZSTD_isError(ret)) {
    return false;
  }

  return true;
}

bool ZstdDictCodec::SetDictionary(const std::string &dict) {
  ZSTD_freeCDict(compression_dict_);
  ZSTD_freeDDict(decompression_dict_);

  dict_.clear();
  compression_dict_ = nullptr;
  decompression_dict_ = nullptr;

  compression_dict_ =
      ZSTD_createCDict(dict.c_str(), dict.size(), compression_level_);
  decompression_dict_ = ZSTD_createDDict(dict.c_str(), dict.size());

  if (!compression_dict_ || !decompression_dict_) {
    return false;
  }

  dict_ = dict;
  return true;
}

std::unique_ptr<CompressionCodec> CompressionCodec::GetCodec(
    const std::string &type) {
  if (type == "LZ4") {
    return std::make_unique<Lz4Codec>();
  } else if (type == "ZSTD") {
    return std::make_unique<ZstdCodec>();
  } else if (type == "ZSTD_DICT") {
    return std::make_unique<ZstdDictCodec>();
  }
  return nullptr;
}
