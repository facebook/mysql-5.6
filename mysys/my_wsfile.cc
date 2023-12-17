// Copyright 2004-present Facebook. All Rights Reserved.

#include "mysys/my_wsfile.h"
#include <errno.h>
#include <my_sys.h>
#include <unistd.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include "include/my_dbug.h"
#include "include/my_thread_local.h"
#include "mysql/psi/mysql_rwlock.h"
#include "mysys/my_wsenv.h"
#include "mysys_priv.h"
#include "rocksdb/env.h"
#include "rocksdb/status.h"
#include "scope_guard.h"

// Current(available) WS file descriptor
File ws_current_fd = WS_START_FD;

class WSBaseFile;
// Map between WS File Descriptor and its WS File wrapper
std::unordered_map<File, std::unique_ptr<WSBaseFile>> ws_file_map;

mysql_rwlock_t ws_current_fd_lock;

// Convert from rocksdb status to system error code
static int get_errno_from_wserr(const rocksdb::Status &status) {
  if (status.IsNoSpace()) return ENOSPC;
  if (status.IsTryAgain()) return EAGAIN;
  if (status.IsNotSupported()) return EPERM;
  if (status.IsInvalidArgument()) return EINVAL;
  if (status.IsIOError() || status.IsIOFenced()) return EIO;
  if (status.IsNotFound()) return ENOENT;
  if (status.IsMemoryLimit()) return ENOMEM;
  if (status.IsBusy()) return EBUSY;
  /* The error code wasn't in the table.  Return EINVAL */
  return EINVAL;
}

class WSBaseFile {
 public:
  WSBaseFile(const std::string &uri) : wsenv_uri(uri), ws_offset(0) {}
  virtual ~WSBaseFile() {}

  // Close a fd. Return 0 if successful. Otherwise return  -1.
  virtual int ws_close() = 0;

  // Write a count of bytes to a file. Return the number of bytes
  // written if successful. Otherwise, return -1.
  virtual size_t ws_write(const uchar *buffer, size_t count) = 0;

  // Read a chunk of bytes from a file. Return the number of bytes
  // read if successful. Otherwise, return -1.
  virtual size_t ws_read(uchar *buffer, size_t count) = 0;

  // Emulate file seek on warm storage. Return the current position
  // if it supports it. Otherwise, return -1.
  virtual my_off_t ws_seek(my_off_t pos, int whence) = 0;

  // Return current position of a warm storage file.
  my_off_t ws_tell() const { return ws_offset; }

  virtual int ws_sync() = 0;
  virtual int ws_fsync() = 0;

 protected:
  // uri format: {wsenv_uri_prefix}/{wsenv_cluster}/{file}
  std::string wsenv_uri;
  // Records the current offset of the warm storage file
  std::atomic<my_off_t> ws_offset;
};

/*
  WSWritableFile is a wrapper class to create WS writtable File
*/
class WSWritableFile : public WSBaseFile {
 public:
  WSWritableFile(const std::string &uri,
                 std::unique_ptr<rocksdb::WritableFile> &&fp)
      : WSBaseFile(uri), fd_ptr(std::move(fp)) {}

  size_t ws_write(const uchar *buffer, size_t count) override {
    rocksdb::Slice data(reinterpret_cast<const char *>(buffer), count);
    rocksdb::Status s = fd_ptr->Append(data);
    if (s.ok()) {
      ws_offset += count;
      return count;
    } else {
      errno = get_errno_from_wserr(s);
      return -1;
    }
  }

  // WritableFile doesn't support read operation. Set errno and
  // return -1.
  size_t ws_read(uchar * /*buffer*/, size_t /*count*/) override {
    errno = EINVAL;
    return -1;
  }

  int ws_close() override {
    rocksdb::Status s = fd_ptr->Close();
    if (s.ok()) {
      return 0;
    } else {
      errno = get_errno_from_wserr(s);
      return -1;
    }
  }

  my_off_t ws_seek(my_off_t pos, int whence) override {
    // WritableFile doesn't allow seek, so just check if this is a no-op.
    // If it is an no-op, skip it. Otherwise fail.
    switch (whence) {
      case SEEK_SET:
        if (pos != ws_offset) {
          errno = EINVAL;
          return -1;
        }
        break;
      case SEEK_CUR:
        if (ws_offset != 0) {
          errno = EINVAL;
          return -1;
        }
        break;
      case SEEK_END:
        if (ws_offset != 0) {
          errno = EINVAL;
          return -1;
        }
        break;
    }
    return ws_offset;
  }

  int ws_sync() override {
    rocksdb::Status s = fd_ptr->Sync();
    if (s.ok()) {
      return 0;
    } else {
      errno = get_errno_from_wserr(s);
      return -1;
    }
  }
  int ws_fsync() override {
    rocksdb::Status s = fd_ptr->Fsync();
    if (s.ok()) {
      return 0;
    } else {
      errno = get_errno_from_wserr(s);
      return -1;
    }
  }

 private:
  std::unique_ptr<rocksdb::WritableFile> fd_ptr;
};

/*
   WSRandomAccessFile is a wrapper class to open/read WS File
*/
class WSRandomAccessFile : public WSBaseFile {
 public:
  WSRandomAccessFile(const std::string &uri,
                     std::unique_ptr<rocksdb::RandomAccessFile> &&fp)
      : WSBaseFile(uri), file_ptr(std::move(fp)) {}

  // RandomAccessFile doesn't support read operation. Set errno and
  // return -1.
  size_t ws_write(const uchar * /*buffer*/, size_t /*count*/) override {
    errno = EINVAL;
    return -1;
  }

  size_t ws_read(uchar *buffer, size_t count) override {
    rocksdb::Slice result;
    rocksdb::Status s = file_ptr->Read(ws_offset, count, &result,
                                       reinterpret_cast<char *>(buffer));
    if (s.ok()) {
      size_t size = result.size();
      ws_offset += size;
      return size;
    } else {
      errno = get_errno_from_wserr(s);
      return -1;
    }
  }

  // RandomAccessFile doesn't provide close interface. Just return 0.
  int ws_close() override { return 0; }

  my_off_t ws_seek(my_off_t pos, int whence) override {
    // Reposition the offsets of RandomAccessFile
    switch (whence) {
      case SEEK_SET:
        ws_offset = pos;
        break;
      case SEEK_CUR:
        ws_offset += pos;
        break;
      case SEEK_END:
        std::string relative;
        const auto env = get_wsenv_from_uri(wsenv_uri, &relative);
        if (env == nullptr) {
          errno = EINVAL;
          return -1;
        }
        // Get warm storage file size, and store result into filesize.
        uint64_t file_size;
        rocksdb::Status s = env->GetFileSize(relative, &file_size);
        if (!s.ok()) {
          errno = get_errno_from_wserr(s);
          return -1;
        }
        ws_offset = file_size + pos;
        break;
    }
    return ws_offset;
  }

  int ws_sync() override {
    errno = EINVAL;
    return -1;
  }

  int ws_fsync() override {
    errno = EINVAL;
    return -1;
  }

 private:
  std::unique_ptr<rocksdb::RandomAccessFile> file_ptr;
};

int my_ws_access(const std::string &uri, int mode) {
  assert(mode == (WS_ACCESS_MODE | F_OK));
  // WS access only check file exists
  if (mode != (WS_ACCESS_MODE | F_OK)) {
    errno = EINVAL;
    return -1;
  }

  std::string file;
  auto env = get_wsenv_from_uri(uri, &file);
  if (env == nullptr) {
    errno = EINVAL;
    return -1;
  }

  My_thd_wait_scope wait(MY_THD_WAIT_WS_IO);
  rocksdb::Status s = env->FileExists(file);

  if (s.ok()) {
    return 0;
  } else {
    errno = get_errno_from_wserr(s);
    return -1;
  }
}

int my_ws_close(File fd) {
  mysql_rwlock_wrlock(&ws_current_fd_lock);
  auto grd =
      create_scope_guard([]() { mysql_rwlock_unlock(&ws_current_fd_lock); });
  // Return error if fd is not present
  auto iter = ws_file_map.find(fd);
  if (iter == ws_file_map.end()) {
    errno = EINVAL;
    return -1;
  }

  My_thd_wait_scope wait(MY_THD_WAIT_WS_IO);
  int result = iter->second->ws_close();
  if (result == 0) {
    ws_file_map.erase(iter);
  }
  return 0;
}

File my_ws_create(const std::string &uri, int access_flags) {
  assert(access_flags & WS_SEQWRT);
  // Quick check access_flags
  if (!(access_flags & WS_SEQWRT)) {
    errno = EINVAL;
    return -1;
  }

  // Check whether uri is file
  if (uri.back() == '/') {
    errno = EISDIR;
    return -1;
  }

  // Fetch ws env and convert uri to relative file in ws
  std::string filename;
  auto env = get_wsenv_from_uri(uri, &filename);
  // Use unique_ptr to manage EnvOptions memory
  std::unique_ptr<rocksdb::EnvOptions> opt(get_wsenv_options());
  if (env == nullptr || opt == nullptr) {
    errno = EINVAL;
    return -1;
  }

  // Protect all network operations with wait scope.
  My_thd_wait_scope wait(MY_THD_WAIT_WS_IO);

  // Create ws directory if necessary
  size_t last = filename.find_last_of('/');
  if (last != std::string::npos) {
    env->CreateDirIfMissing(filename.substr(0, last));
  }

  // Create ws file
  std::unique_ptr<rocksdb::WritableFile> ws_file;
  rocksdb::Status s = env->NewWritableFile(filename, &ws_file, *opt);

  if (s.ok()) {
    mysql_rwlock_wrlock(&ws_current_fd_lock);
    File fd = ws_current_fd++;
    ws_file_map[fd].reset(new WSWritableFile(uri, std::move(ws_file)));
    mysql_rwlock_unlock(&ws_current_fd_lock);
    return fd;
  } else {
    errno = get_errno_from_wserr(s);
    return -1;
  }
}

int my_ws_fsync(File fd) {
  mysql_rwlock_rdlock(&ws_current_fd_lock);
  auto grd =
      create_scope_guard([]() { mysql_rwlock_unlock(&ws_current_fd_lock); });

  auto iter = ws_file_map.find(fd);
  // Return error if fd is not present
  if (iter == ws_file_map.end()) {
    errno = EINVAL;
    return -1;
  }

  My_thd_wait_scope wait(MY_THD_WAIT_WS_IO);
  int result = iter->second->ws_fsync();
  return result;
}

File my_ws_open(const std::string &uri, int access_flags) {
  assert(access_flags & WS_RNDRD);
  // quick check access_flags
  if (!(access_flags & WS_RNDRD)) {
    errno = EINVAL;
    return -1;
  }

  // create ws env and convert uri to relative file in ws
  std::string filename;
  const auto env = get_wsenv_from_uri(uri, &filename);
  std::unique_ptr<rocksdb::EnvOptions> opt(get_wsenv_options());
  if (env == nullptr || opt == nullptr) {
    errno = EINVAL;
    return -1;
  }

  // open existing file in WS
  My_thd_wait_scope wait(MY_THD_WAIT_WS_IO);
  std::unique_ptr<rocksdb::RandomAccessFile> ws_file;
  rocksdb::Status s = env->NewRandomAccessFile(filename, &ws_file, *opt);
  if (s.ok()) {
    mysql_rwlock_wrlock(&ws_current_fd_lock);
    File fd = ws_current_fd++;
    ws_file_map[fd].reset(new WSRandomAccessFile(uri, std::move(ws_file)));
    mysql_rwlock_unlock(&ws_current_fd_lock);
    return fd;
  } else {
    errno = get_errno_from_wserr(s);
    return -1;
  }
}

size_t my_ws_read(File fd, uchar *buffer, size_t count) {
  mysql_rwlock_rdlock(&ws_current_fd_lock);
  auto grd =
      create_scope_guard([]() { mysql_rwlock_unlock(&ws_current_fd_lock); });
  auto iter = ws_file_map.find(fd);
  // Return error if fd is not present
  if (iter == ws_file_map.end()) {
    errno = EINVAL;
    return -1;
  }

  My_thd_wait_scope wait(MY_THD_WAIT_WS_IO);
  size_t result = iter->second->ws_read(buffer, count);
  return result;
}

my_off_t my_ws_seek(File fd, my_off_t pos, int whence) {
  mysql_rwlock_rdlock(&ws_current_fd_lock);
  auto grd =
      create_scope_guard([]() { mysql_rwlock_unlock(&ws_current_fd_lock); });
  auto iter = ws_file_map.find(fd);
  // Return error if fd is not present
  if (iter == ws_file_map.end()) {
    errno = EINVAL;
    return -1;
  }

  My_thd_wait_scope wait(MY_THD_WAIT_WS_IO);
  my_off_t result = iter->second->ws_seek(pos, whence);
  return result;
}

int my_ws_sync(File fd) {
  mysql_rwlock_rdlock(&ws_current_fd_lock);
  auto grd =
      create_scope_guard([]() { mysql_rwlock_unlock(&ws_current_fd_lock); });
  auto iter = ws_file_map.find(fd);
  // Return error if fd is not present
  if (iter == ws_file_map.end()) {
    errno = EINVAL;
    return -1;
  }

  My_thd_wait_scope wait(MY_THD_WAIT_WS_IO);
  int result = iter->second->ws_sync();
  return result;
}

my_off_t my_ws_tell(File fd) {
  mysql_rwlock_rdlock(&ws_current_fd_lock);
  auto grd =
      create_scope_guard([]() { mysql_rwlock_unlock(&ws_current_fd_lock); });
  auto iter = ws_file_map.find(fd);
  // Return error if fd is not present
  if (iter == ws_file_map.end()) {
    errno = EINVAL;
    return -1;
  }

  My_thd_wait_scope wait(MY_THD_WAIT_WS_IO);
  my_off_t offset = iter->second->ws_tell();
  return offset;
}

size_t my_ws_write(File fd, const uchar *buffer, size_t count) {
  mysql_rwlock_rdlock(&ws_current_fd_lock);
  auto grd =
      create_scope_guard([]() { mysql_rwlock_unlock(&ws_current_fd_lock); });
  auto iter = ws_file_map.find(fd);
  // Return error if fd is not present
  if (iter == ws_file_map.end()) {
    errno = EINVAL;
    return -1;
  }

  My_thd_wait_scope wait(MY_THD_WAIT_WS_IO);
  size_t result = iter->second->ws_write(buffer, count);
  return result;
}

void MyWSFileEnd() { ws_file_map.clear(); }
