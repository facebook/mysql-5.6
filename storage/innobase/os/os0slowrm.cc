/*
   Copyright (c) 2019, Facebook, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

/*
 * Original algorithm
 * https://github.com/midom/slowrm
 *
 * Slow removal of big files
 * 1. check file size > @@global.innodb_big_file_slow_removal_speed
 * 2. move file to DATADIR/.slowrm/ directory
 * 3. background thread every 10 seconds scans that directory
 * and slowrm all files
 */

#include <algorithm> /* std::basic_string */

#include "os0file.h"
#include "sql_thd_internal_api.h"
#include "srv0srv.h"
#include "srv0start.h"

#include "current_thd.h" /* current_thd */
#include "debug_sync.h"  /* debug_sync_set_action */

/** Event to signal the slow removal thread. */
os_event_t srv_slowrm_event;

static constexpr uint64 MICROSECS_IN_SEC = 1000000;

static constexpr uint32 megabyte = 1024 * 1024;
static constexpr uint32 thread_sleep_interval = 10000000; /* microseconds */
static constexpr uint32 slowrm_interval = 100000;         /* microseconds */

/** Directory will be created in Innodb root directory where we have database
directories (test, mysql, ...) */
static constexpr char slowrm_dir[] = "./.slowrm";

static ut::string make_path(const ut::string &dirname,
                            const ut::string &filename,
                            const ut::string &extension = {}) {
  ut::string path;
  auto path_len = dirname.size() + filename.size() + extension.size() + 2;

  path.reserve(path_len);
  path += dirname;
  path += OS_PATH_SEPARATOR;
  path += filename;
  if (!extension.empty()) {
    path += '.';
    path += extension;
  }
  return path;
}

static inline int64 chunk_size_for_slow_rm() {
  return srv_slowrm_speed_mbps * megabyte;
}

int slowfileremove(const char *filename) {
  uint64 usec;
  struct timeval tv;
  struct stat statinfo;
  ut::string filetoremove, fname{filename};

  if (!srv_slowrm_speed_mbps) goto out;

  /* If file is not regular or smaller then chunk size, just delete it. */
  if (stat(filename, &statinfo) || !S_ISREG(statinfo.st_mode) ||
      statinfo.st_size < chunk_size_for_slow_rm())
    goto out;

  /* Files submitted for removal may contain directory name convert path to
  file into simple filename "./test/t1.ibd" -> "__test_t1_ibd". */
  std::replace_if(
      fname.begin(), fname.end(), [](char c) { return c == '.' || c == '/'; },
      '_');

  if (gettimeofday(&tv, NULL)) {
    char *reason = strerror(errno);
    ib::error(ER_IB_SLOWRM_GET_TIME)
        << "slowfileremove failed to execute gettimeofday: '" << reason << "'";
    goto out;
  }

  usec = tv.tv_sec * MICROSECS_IN_SEC + tv.tv_usec;
  {
    ut::ostringstream oss;
    oss << usec;
    filetoremove = make_path(slowrm_dir, fname, oss.str());
  }

  /* Create slowrm_dir directory if it does not exist. */
  if (os_file_create_subdirs_if_needed(filetoremove.c_str()) != DB_SUCCESS) {
    ib::error(ER_IB_SLOWRM_CREATE_DIR) << "slowfileremove could not create "
                                       << "directory: '" << slowrm_dir << "'";
    goto out;
  }

  /* Move file to slowrm_dir. */
  if (rename(filename, filetoremove.c_str())) {
    char *reason = strerror(errno);
    ib::error(ER_IB_SLOWRM_MOVE_FILE)
        << "slowfileremove could not move file: '" << filename << "' to '"
        << filetoremove << "'\nreason: '" << reason << "'";
    goto out;
  }

  return 0;

out:
  /* If rename failed fall back to unlink. */
  return unlink(filename);
}

static void slowrm(const ut::string &path, off_t size) {
  int fd = open(path.c_str(), O_RDWR);

  if (fd < 0) return;

  while (size > chunk_size_for_slow_rm() &&
         srv_shutdown_state == SRV_SHUTDOWN_NONE) {
    /* Convert bytes per seconds into bytes per 0.1 second;
    max speed 100000 (100 Gbps),
    so (speed * 1000000 / 10) cannot produce ulonglong overflow. */
    int64 chunk_size = chunk_size_for_slow_rm() / 10;

    if (chunk_size == 0) /* srv_slowrm_speed_mbps is 0 */
      break;
    if (chunk_size > size)
      size = 0;
    else
      size -= chunk_size;
    if (ftruncate(fd, size) < 0) break;
    fsync(fd);
    usleep(slowrm_interval);
  }

  /* If we finished truncating earlier because of server shutdown
  and file size <= chunk size, then delete it. */
  if (size <= chunk_size_for_slow_rm()) unlink(path.c_str());

  close(fd);

  DBUG_EXECUTE_IF(
      "ib_os_big_file_slow_removal",
      ut_ad(!debug_sync_set_action(
          current_thd, STRING_WITH_LEN("now SIGNAL big_file_removed"))););
}

static void remove_file_cb(const char *dirname, const char *fname) {
  ut::string path;
  struct stat statinfo;
  const ut::string ut_fname{fname};

  ut_a(ut_fname.length() < OS_FILE_MAX_PATH);

  if (srv_shutdown_state != SRV_SHUTDOWN_NONE) return;

  path = make_path(dirname, ut_fname);

  /* If readdir() returned a file that does not exist, it must have been
  deleted in the meantime. The behaviour in this case should be the same
  as if the file was deleted before readdir(): ignore and go to the next
  entry. */
  if (stat(path.c_str(), &statinfo)) {
    if (errno != ENOENT) {
      os_file_handle_error_no_exit(path.c_str(), "stat", false);
      return;
    }
  } else if (S_ISREG(statinfo.st_mode)) {
    slowrm(path, statinfo.st_size);
  }

  return;
}

/** Background thread which slowly truncates files in slowrm_dir. */
void srv_slowrm_thread() {
  auto thd_deleter = [](THD *thd) { destroy_thd(thd); };
  const PSI_thread_key psi_key =
#ifdef UNIV_PFS_THREAD
      srv_slowrm_thread_key.m_value
#else
      0
#endif
      ;

  using thd_ptr = std::unique_ptr<THD, decltype(thd_deleter)>;
  thd_ptr thd_capsule{create_thd(false, true, true, psi_key, 0),
                      std::move(thd_deleter)};
  int64_t sig_count = 0;

  do {
    /* sleep thread_sleep_interval microseconds */
    os_event_wait_time_low(srv_slowrm_event,
                           std::chrono::microseconds{thread_sleep_interval},
                           sig_count);
    sig_count = os_event_reset(srv_slowrm_event);

    if (srv_shutdown_state != SRV_SHUTDOWN_NONE) break;

    os_file_scan_directory(slowrm_dir, remove_file_cb, false, false);

  } while (srv_shutdown_state == SRV_SHUTDOWN_NONE);
}
