/* Copyright (c) 2018, 2022, Oracle and/or its affiliates.

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

#ifdef HAVE_THRIFT
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winconsistent-missing-override"
#pragma clang diagnostic ignored "-Winconsistent-missing-destructor-override"
#pragma clang diagnostic ignored "-Wcast-qual"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winconsistent-missing-override"
#pragma GCC diagnostic ignored "-Winconsistent-missing-destructor-override"
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
#include <sql/gen/LogWrapper_types.h>
#include <thrift/protocol/TCompactProtocol.h>
#include <thrift/transport/TBufferTransports.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#undef PACKAGE
#undef PACKAGE_VERSION
#endif

#include <boost/endian/conversion.hpp>

#include "sql/binlog_istream.h"
#include "sql/log_event.h"
#include "sql/mysqld.h"
#ifdef MYSQL_SERVER
#include "my_aes.h"
#include "mysql/components/services/log_builtins.h"
#include "sql/binlog.h"
#include "sql/mysqld.h"
#endif

const char *Binlog_read_error::get_str() const {
  switch (m_type) {
    case READ_EOF:
      return "arrived the end of the file";
    case BOGUS:
      return "corrupted data in log event";
    case SYSTEM_IO:
      return "I/O error reading log event";
    case EVENT_TOO_LARGE:
      return "Event too big";
    case MEM_ALLOCATE:
      return "memory allocation failed reading log event";
    case TRUNC_EVENT:
      return "binlog truncated in the middle of event; consider out of disk "
             "space";
    case TRUNC_FD_EVENT:
      return "Found invalid Format description event in binary log";
    case CHECKSUM_FAILURE:
      return "Event crc check failed! Most likely there is event corruption.";
    case INVALID_EVENT:
      return "Found invalid event in binary log";
    case CANNOT_OPEN:
      return "Could not open log file";
    case HEADER_IO_FAILURE:
      return "I/O error reading the header from the binary log";
    case BAD_BINLOG_MAGIC:
      return "Binlog has bad magic number;  It's not a binary log file "
             "that can be used by this version of MySQL";
    case INVALID_ENCRYPTION_HEADER:
      return "Found invalid binary log encryption header";
    case CANNOT_GET_FILE_PASSWORD:
      return "Cannot get file password for encrypted replication log file, "
             "please check if keyring plugin is loaded";
    case READ_ENCRYPTED_LOG_FILE_IS_NOT_SUPPORTED:
      return "Reading encrypted log files directly is not supported.";
    case ERROR_DECRYPTING_FILE:
      return "Failed to decrypt content read from binlog file.";
    default:
      /* There must be something wrong in the code if it reaches this branch. */
      assert(0);
      return nullptr;
  }
}

bool Binlog_encryption_istream::open(
    std::unique_ptr<Basic_seekable_istream> down_istream,
    Binlog_read_error *binlog_read_error) {
  DBUG_TRACE;
  assert(down_istream != nullptr);

  m_down_istream = std::move(down_istream);

  std::unique_ptr<Rpl_encryption_header> encryption_header(
      Rpl_encryption_header::get_header(m_down_istream.get()));

  if (encryption_header == nullptr)
    return binlog_read_error->set_type(
        Binlog_read_error::INVALID_ENCRYPTION_HEADER);

  Key_string password = encryption_header->decrypt_file_password();
  if (password.empty())
    return binlog_read_error->set_type(
        Binlog_read_error::CANNOT_GET_FILE_PASSWORD);

  m_decryptor = encryption_header->get_decryptor();
  if (m_decryptor->open(password, encryption_header->get_header_size())) {
    m_decryptor.reset(nullptr);
    return binlog_read_error->set_type(
        Binlog_read_error::ERROR_DECRYPTING_FILE);
  }

  return false;
}

void Binlog_encryption_istream::close() {
  DBUG_TRACE;
  m_down_istream.reset(nullptr);
  m_decryptor.reset(nullptr);
}

ssize_t Binlog_encryption_istream::read(unsigned char *buffer, size_t length) {
  DBUG_TRACE;
  ssize_t ret = m_down_istream->read(buffer, length);
  if (ret > 0 && m_decryptor->decrypt(buffer, buffer, ret)) ret = -1;
  return ret;
}

bool Binlog_encryption_istream::seek(my_off_t offset) {
  DBUG_TRACE;
  DBUG_PRINT("debug", ("offset= %llu", offset));
  bool res = m_decryptor->set_stream_offset(offset);
  if (!res) res = m_down_istream->seek(offset + m_decryptor->get_header_size());
  return res;
}

my_off_t Binlog_encryption_istream::length() {
  return m_down_istream->length() - m_decryptor->get_header_size();
}

Binlog_encryption_istream::~Binlog_encryption_istream() { close(); }

bool Basic_binlog_ifile::read_binlog_magic() {
  DBUG_TRACE;
  unsigned char magic[BINLOG_MAGIC_SIZE];

  if (m_istream->read(magic, BINLOG_MAGIC_SIZE) != BINLOG_MAGIC_SIZE) {
    return m_error->set_type(Binlog_read_error::HEADER_IO_FAILURE);
  }

  /*
    If this is an encrypted stream, read encryption header and setup up
    encryption stream pipeline.
  */
  if (memcmp(magic, Rpl_encryption_header::ENCRYPTION_MAGIC,
             Rpl_encryption_header::ENCRYPTION_MAGIC_SIZE) == 0) {
#ifdef MYSQL_SERVER
    std::unique_ptr<Binlog_encryption_istream> encryption_istream{
        new Binlog_encryption_istream()};
    if (encryption_istream->open(std::move(m_istream), m_error)) return true;

    /* Setup encryption stream pipeline */
    m_istream = std::move(encryption_istream);

    /* Read binlog magic from encrypted data */
    if (m_istream->read(magic, BINLOG_MAGIC_SIZE) != BINLOG_MAGIC_SIZE) {
      return m_error->set_type(Binlog_read_error::BAD_BINLOG_MAGIC);
    }
#else
    return m_error->set_type(
        Binlog_read_error::READ_ENCRYPTED_LOG_FILE_IS_NOT_SUPPORTED);
#endif
  }

  if (memcmp(magic, BINLOG_MAGIC, BINLOG_MAGIC_SIZE)) {
#ifdef HAVE_THRIFT
    // assuming it is a thrift file
    std::unique_ptr<Binlog_thrift_istream> thrift_istream{
        new Binlog_thrift_istream()};
    if (thrift_istream->open(std::move(m_istream), m_error)) return true;

    /* Setup thrift stream pipeline */
    m_istream = std::move(thrift_istream);

    /* Read binlog magic from thrift data */
    if (m_istream->read(magic, BINLOG_MAGIC_SIZE) != BINLOG_MAGIC_SIZE) {
      return m_error->set_type(Binlog_read_error::BAD_BINLOG_MAGIC);
    }
#else
    return m_error->set_type(Binlog_read_error::BAD_BINLOG_MAGIC);
#endif  // HAVE_THRIFT
  }
  m_position = BINLOG_MAGIC_SIZE;
  return m_error->set_type(Binlog_read_error::SUCCESS);
}

Basic_binlog_ifile::Basic_binlog_ifile(Binlog_read_error *binlog_read_error)
    : m_error(binlog_read_error) {
  DBUG_TRACE;
}

Basic_binlog_ifile::~Basic_binlog_ifile() {
  DBUG_TRACE;
  close();
}

bool Basic_binlog_ifile::open(const char *file_name) {
  m_istream = open_file(file_name);
  if (m_istream == nullptr)
    return m_error->set_type(Binlog_read_error::CANNOT_OPEN);
  return read_binlog_magic();
}

void Basic_binlog_ifile::close() {
  DBUG_TRACE;
  m_position = 0;
  m_istream.reset(nullptr);
}

ssize_t Basic_binlog_ifile::read(unsigned char *buffer, size_t length) {
  longlong ret = m_istream->read(buffer, length);
  if (ret > 0) m_position += ret;
  return ret;
}

bool Basic_binlog_ifile::seek(my_off_t position) {
  if (m_istream->seek(position)) {
    m_error->set_type(Binlog_read_error::SYSTEM_IO);
    return true;
  }
  m_position = position;
  return false;
}

my_off_t Basic_binlog_ifile::length() { return m_istream->length(); }

#ifdef MYSQL_SERVER

std::unique_ptr<Basic_seekable_istream> Binlog_ifile::open_file(
    const char *file_name) {
  IO_CACHE_istream *ifile = new IO_CACHE_istream;
  if (ifile->open(key_file_binlog, key_file_binlog_cache, file_name,
                  MYF(MY_WME | MY_DONT_CHECK_FILESIZE), rpl_read_size)) {
    delete ifile;
    return nullptr;
  }
  return std::unique_ptr<Basic_seekable_istream>(ifile);
}

std::unique_ptr<Basic_seekable_istream> Relaylog_ifile::open_file(
    const char *file_name) {
  IO_CACHE_istream *ifile = new IO_CACHE_istream;
  if (ifile->open(key_file_relaylog, key_file_relaylog_cache, file_name,
                  MYF(MY_WME | MY_DONT_CHECK_FILESIZE), rpl_read_size)) {
    delete ifile;
    return nullptr;
  }
  return std::unique_ptr<Basic_seekable_istream>(ifile);
}

#endif  // ifdef MYSQL_SERVER

#ifdef HAVE_THRIFT
Binlog_thrift_istream::~Binlog_thrift_istream() { close(); }

bool Binlog_thrift_istream::open(
    std::unique_ptr<Basic_seekable_istream> down_istream,
    Binlog_read_error * /*binlog_read_error*/) {
  m_down_istream = std::move(down_istream);
  if (!m_down_istream) {
    return true;
  }
  return m_down_istream->seek(0);
}

void Binlog_thrift_istream::close() {
  m_down_istream.reset(nullptr);
  m_codec.reset(nullptr);
}

ssize_t Binlog_thrift_istream::read(unsigned char *buffer, size_t length) {
  ssize_t ret = 0;
  while (length) {
    // Case: we have stuff in buf_ that can be copied over
    if (buf_offset_ < buf_.size()) {
      size_t copy_size = std::min(length, buf_.size() - buf_offset_);
      memcpy(buffer, buf_.c_str() + buf_offset_, copy_size);
      buf_offset_ += copy_size;
      buffer += copy_size;
      length -= copy_size;
      ret += copy_size;
      continue;
    }

    assert(buf_offset_ == buf_.size());
    // reset offset now that we're going to read a fresh buffer
    buf_offset_ = 0;

    // read the size of the next thrift obj
    uint8_t size_buf[sizeof(uint32_t)];
    memset(size_buf, 0, sizeof(size_buf));
    uint32_t bytes_read = m_down_istream->read(size_buf, sizeof(uint32_t));
    if (bytes_read == 0) {
      break;
    }
    if (bytes_read != sizeof(uint32_t)) {
      return -1;
    }

    uint32_t size = boost::endian::big_to_native(
        *reinterpret_cast<const uint32_t *>(size_buf));

    // read the thrift obj into a buffer
    auto data_buf = std::make_unique<unsigned char[]>(size);
    bytes_read = m_down_istream->read(data_buf.get(), size);
    if (bytes_read != size) {
      return -1;
    }

    // deserialize the thrift obj
    using namespace apache::thrift::transport;
    using namespace apache::thrift::protocol;
    std::shared_ptr<TMemoryBuffer> transport =
        std::make_shared<TMemoryBuffer>();
    transport->resetBuffer(reinterpret_cast<uint8_t *>(data_buf.get()), size);
    std::shared_ptr<TCompactProtocol> protocol =
        std::make_shared<TCompactProtocol>(transport);

    LogEntry entry;
    try {
      entry.read(protocol.get());
    } catch (const apache::thrift::TException &ex) {
      return -1;
    }

    if (!entry.__isset.data) {
      return -1;
    }

    LogData &data = entry.data;

    if (!data.__isset.payload) {
      return -1;
    }

    if (!entry.__isset.metadata) {
      buf_ = std::move(data.payload);
      continue;
    }

    const LogMetadata &metadata = entry.metadata;

    if (!metadata.__isset.compression_codec ||
        metadata.compression_codec == "NO_COMPRESSION") {
      buf_ = std::move(data.payload);
      continue;
    }

    if (!m_codec || metadata.compression_codec != m_codec->type()) {
      m_codec = CompressionCodec::GetCodec(metadata.compression_codec);
      if (!m_codec) return -1;
    }

    if (!metadata.__isset.uncompressed_size) {
      return -1;
    }

    if (metadata.__isset.compression_dict) {
      m_codec->SetDictionary(metadata.compression_dict);
      if (metadata.__isset.compression_dict_id &&
          metadata.compression_dict_id != m_codec->GetDictionaryID()) {
        return -1;
      }
      unsigned int dict_id = metadata.__isset.compression_dict_id
                                 ? metadata.compression_dict_id
                                 : m_codec->GetDictionaryID();
      last_compression_dict_id = dict_id;
    }

    const int64_t uncompressed_size = metadata.uncompressed_size;
    buf_.resize(uncompressed_size);
    if (!m_codec->Uncompress(data.payload, (uint8_t *)buf_.data(),
                             uncompressed_size)) {
      return -1;
    }
  }
  return ret;
}

bool Binlog_thrift_istream::seek(my_off_t offset) {
  auto data_buf = std::make_unique<unsigned char[]>(offset);
  return read(data_buf.get(), offset) == -1;
}

my_off_t Binlog_thrift_istream::length() {
  unsigned char buf[1024];
  my_off_t length = 0;
  ssize_t bytes_read = 0;
  while ((bytes_read = read(buf, sizeof(buf))) != 0) {
    length += bytes_read;
  }
  return length;
}
#endif  // HAVE_THRIFT
