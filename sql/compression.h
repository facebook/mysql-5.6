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

#pragma once

#include <memory>
#include <string>

#include <lz4.h>
#include <zstd.h>

class CompressionCodec {
 public:
  CompressionCodec() = default;
  virtual ~CompressionCodec() = default;

  static std::unique_ptr<CompressionCodec> GetCodec(const std::string &type);

  virtual bool Compress(const std::string &input, uint8_t *compressed,
                        size_t *compressed_length) = 0;

  virtual bool Uncompress(const std::string &compressed, uint8_t *uncompressed,
                          size_t uncompressed_length) = 0;

  // Returns the maximal size of the compressed representation of
  // input data that is "source_bytes" bytes in length.
  virtual size_t MaxCompressedLength(size_t source_bytes) const = 0;

  // Sets a compression dictionary
  virtual bool SetDictionary(const std::string & /*dict*/) { return true; }

  // Returns dictionary if dictionary compression is used
  virtual std::string GetDictionary() const { return {}; }

  virtual unsigned int GetDictionaryID() const { return 0; }

  // Sets compression level
  virtual bool SetCompressionLevel(int level) {
    compression_level_ = level;
    return true;
  }

  virtual int CompressionLevel() const { return compression_level_; }

  // Return the type of compression implemented by this codec.
  virtual std::string type() const = 0;

 protected:
  int compression_level_ = 0;
};

class Lz4Codec : public CompressionCodec {
 public:
  bool Compress(const std::string &input, uint8_t *compressed,
                size_t *compressed_length) override;

  bool Uncompress(const std::string &compressed, uint8_t *uncompressed,
                  size_t uncompressed_length) override;

  size_t MaxCompressedLength(size_t source_bytes) const override {
    return LZ4_compressBound(source_bytes);
  }

  bool SetCompressionLevel(int level) override {
    if (level < 0) {
      return false;
    }
    compression_level_ = level;
    return true;
  }

  std::string type() const override { return "LZ4"; }
};

class ZstdCodec : public CompressionCodec {
 public:
  ZstdCodec() {
    compression_ctx_ = ZSTD_createCCtx();
    decompression_ctx_ = ZSTD_createDCtx();
    compression_level_ = 1;
  }

  ~ZstdCodec() override {
    ZSTD_freeCCtx(compression_ctx_);
    ZSTD_freeDCtx(decompression_ctx_);
  }

  bool Compress(const std::string &input, uint8_t *compressed,
                size_t *compressed_length) override;

  bool Uncompress(const std::string &compressed, uint8_t *uncompressed,
                  size_t uncompressed_length) override;

  size_t MaxCompressedLength(size_t source_bytes) const override {
    return ZSTD_compressBound(source_bytes);
  }

  bool SetCompressionLevel(int level) override {
    if (level < ZSTD_minCLevel() || level > ZSTD_maxCLevel()) {
      return false;
    }
    compression_level_ = level;
    return true;
  }

  std::string type() const override { return "ZSTD"; }

 private:
  ZSTD_CCtx *compression_ctx_ = nullptr;
  ZSTD_DCtx *decompression_ctx_ = nullptr;
};

class ZstdDictCodec : public CompressionCodec {
 public:
  ZstdDictCodec() {
    compression_level_ = 1;
    c_ctx_ = ZSTD_createCCtx();
    d_ctx_ = ZSTD_createDCtx();
    SetDictionary("");
  }

  virtual ~ZstdDictCodec() override {
    ZSTD_freeCCtx(c_ctx_);
    ZSTD_freeDCtx(d_ctx_);
    ZSTD_freeCDict(compression_dict_);
    ZSTD_freeDDict(decompression_dict_);
  }

  bool Compress(const std::string &input, uint8_t *compressed,
                size_t *compressed_length) override;

  bool Uncompress(const std::string &compressed, uint8_t *uncompressed,
                  size_t uncompressed_length) override;

  size_t MaxCompressedLength(size_t source_bytes) const override {
    return ZSTD_compressBound(source_bytes);
  }

  bool SetDictionary(const std::string &dict) override;

  std::string GetDictionary() const override { return dict_; }

  unsigned int GetDictionaryID() const override {
    return ZSTD_getDictID_fromDict(dict_.data(), dict_.size());
  }

  bool SetCompressionLevel(int level) override {
    if (level < ZSTD_minCLevel() || level > ZSTD_maxCLevel()) {
      return false;
    }
    compression_level_ = level;
    std::string dict = dict_;
    return SetDictionary(dict);
  }

  std::string type() const override { return "ZSTD_DICT"; }

 private:
  std::string dict_;

  ZSTD_CDict *compression_dict_ = nullptr;
  ZSTD_DDict *decompression_dict_ = nullptr;
  ZSTD_CCtx *c_ctx_ = nullptr;
  ZSTD_DCtx *d_ctx_ = nullptr;
};
