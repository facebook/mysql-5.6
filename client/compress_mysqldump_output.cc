/*
   Copyright (c) 2018, Facebook, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
*/

#include "my_attribute.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <zstd.h>

extern "C" void verbose_msg(const char *fmt, ...);
extern "C" const char *my_progname;

static void print_sys_error() {
  // NO_LINT_DEBUG
  fprintf(stderr, "%s: %s\n", my_progname, strerror(errno));
}

class compressor {
  static const int compression_level = 3; // ZSTD_CLEVEL_DEFAULT;
  static const unsigned long long megabyte = 1024 * 1024;

public:
  static std::unique_ptr<compressor> create(const std::string &fifo_filename,
                                            unsigned int chunk_size) {
    const std::string out_filename_pattern = fifo_filename + ".%d.zst";
    const long long max_file_size = (long long)(chunk_size * megabyte);
    return std::unique_ptr<compressor>(
        new compressor(out_filename_pattern, max_file_size));
  }
  void add_line(std::string &line, bool end_of_row) {
    size_t pos = 0;
    size_t lsz = line.size();
    while (lsz + m_buf_pos > m_buf_size) {
      size_t len = m_buf_size - m_buf_pos;
      copy_to_buffer(&line[pos], len, false);
      lsz -= len;
      pos += len;
    }
    copy_to_buffer(&line[pos], lsz, end_of_row);
  }
  void close() { compress_and_write_buffer(true, true); }
  ~compressor() { ZSTD_freeCStream(m_cstream); }

private:
  compressor(const std::string &_out_filename_pattern, long long _max_file_size)
      : out_filename_pattern(_out_filename_pattern),
        max_file_size(_max_file_size), m_cstream(ZSTD_createCStream()),
        m_out_filename(_out_filename_pattern.size() + 12, '\0') {
    if (m_cstream == NULL) {
      // NO_LINT_DEBUG
      fprintf(stderr, "ZSTD_createCStream() error \n");
      exit(10);
    }
    m_buf_in.reserve(buf_in_size);
    m_buf_out.reserve(buf_out_size);
    open_next_file_and_zstd_stream();
  }
  void copy_to_buffer(const char *line, size_t len, bool can_close_file) {
    assert((len + m_buf_pos) <= m_buf_size);
    memcpy(&m_buf_in[m_buf_pos], line, len);
    m_buf_pos += len;
    if (m_file_already_big || m_buf_pos == m_buf_size)
      compress_and_write_buffer(can_close_file, false);
  }
  void compress_and_write_buffer(bool can_close_file, bool end_of_input) {
    ZSTD_inBuffer input = {&m_buf_in[0], m_buf_pos, 0};
    // while some data in the input buffer
    while (input.pos < input.size) {
      ZSTD_outBuffer output = {&m_buf_out[0], buf_out_size, 0};
      // compress data, new recommended buf_size is returned
      m_buf_size = ZSTD_compressStream(m_cstream, &output, &input);
      if (ZSTD_isError(m_buf_size)) {
        // NO_LINT_DEBUG
        fprintf(stderr, "ZSTD_compressStream() error : %s \n",
                ZSTD_getErrorName(m_buf_size));
        exit(1);
      }
      // set correct buf_size if it's bigger than allocated size
      if (m_buf_size > buf_in_size)
        m_buf_size = buf_in_size;
      m_outfile.write(&m_buf_out[0], output.pos);
    }
    const long long written_bytes = m_outfile.tellp();
    m_file_already_big = written_bytes > max_file_size;
    // if file bigger than max size or no more input data
    // close zstd stream and file
    if ((m_file_already_big && can_close_file) || end_of_input) {
      // flushing stream buffer and close stream
      ZSTD_outBuffer output = {&m_buf_out[0], buf_out_size, 0};
      size_t const remainingToFlush = ZSTD_endStream(m_cstream, &output);
      // if some data still remains in the stream after flush ...
      if (remainingToFlush) {
        // NO_LINT_DEBUG
        fprintf(stderr, "ZSTD file not fully flushed\n");
        exit(1);
      }
      // write flushed stream buffer into output file and close it
      m_outfile.write(&m_buf_out[0], output.pos);
      const long long fsize = m_outfile.tellp();
      m_outfile.close();
      m_outfile.clear();

      verbose_msg("%s %lld\n", m_out_filename.c_str(), fsize);

      if (!end_of_input)
        open_next_file_and_zstd_stream();
    }
    // reset buf_pos
    m_buf_pos = 0;
  }
  void open_next_file_and_zstd_stream() {
    m_file_index_num++;
    size_t size = out_filename_pattern.size() + 12;
    int n MY_ATTRIBUTE((unused)) =
        snprintf(&m_out_filename[0], size - 1, out_filename_pattern.c_str(),
                 m_file_index_num);
    assert(n > -1);

    m_outfile.open(m_out_filename, std::ofstream::binary);
    if (!m_outfile) {
      print_sys_error();
      exit(1);
    }
    size_t rc = ZSTD_initCStream(m_cstream, compression_level);
    if (ZSTD_isError(rc)) {
      // NO_LINT_DEBUG
      fprintf(stderr, "ZSTD_initCStream() error : %s \n",
              ZSTD_getErrorName(rc));
      exit(1);
    }
  }

  const size_t buf_in_size = ZSTD_CStreamInSize();
  const size_t buf_out_size = ZSTD_CStreamOutSize();
  const std::string out_filename_pattern;
  const long long max_file_size;

  std::vector<char> m_buf_in;
  std::vector<char> m_buf_out;
  ZSTD_CStream *m_cstream;
  size_t m_buf_size = buf_in_size;
  size_t m_buf_pos = 0;
  int m_file_index_num = 0;
  bool m_file_already_big = false;
  std::ofstream m_outfile;
  std::string m_out_filename;
};

struct compress_context {
  std::thread compress_thread;
  std::string fifo_filename;
  std::string tablename;
  unsigned int chunk_size;

  compress_context(const char *_filename, unsigned int _chunk_size,
                   const char *_tablename)
      : fifo_filename(_filename), tablename(_tablename),
        chunk_size(_chunk_size) {}

  void make_fifo_or_die() {
    int rc = mkfifo(fifo_filename.c_str(), 0666);
    if (rc < 0) {
      print_sys_error();
      exit(1);
    }
  }
  void start() {
    make_fifo_or_die();
    compress_thread =
        std::thread(&compress_context::read_pipe_and_compress, this);
  }
  void finish() {
    compress_thread.join();
    std::remove(fifo_filename.c_str());
  }
  void read_pipe_and_compress() {
    std::ifstream fifo(fifo_filename);
    if (!fifo.is_open()) {
      print_sys_error();
      exit(1);
    }
    verbose_msg("start table %s\n", tablename.c_str());

    auto compressor = compressor::create(fifo_filename, chunk_size);
    std::string line;
    while (getline(fifo, line)) {
      const bool end_of_row = line.back() != '\\';
      line.push_back('\n');
      compressor->add_line(line, end_of_row);
    }
    compressor->close();

    verbose_msg("end table %s\n", tablename.c_str());
    fifo.close();
  }
};

extern "C" struct compress_context *
start_pipe_and_compress_output(const char *filename, unsigned int chunk_size,
                               const char *tablename) {
  compress_context *ctx = new compress_context(filename, chunk_size, tablename);
  ctx->start();
  return ctx;
}

extern "C" void finish_pipe_and_compress_output(struct compress_context *ctx) {
  std::unique_ptr<compress_context> context(ctx);
  context->finish();
}
