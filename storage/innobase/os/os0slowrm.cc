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

#include "os0file.h"
#include "srv0srv.h"
#include "srv0start.h"

static const int megabyte = 1024 * 1024;
static const int thread_sleep_interval = 10000000; // microseconds

// directory will be created in Innodb root directory
// where we have database directories (test, mysql, ...)
static const char *slowrm_dir = "./.slowrm/";

static char* make_path(const char *dirname,
    const char *filename, const char *extension)
{
  uint dirname_len = strlen(dirname);
  uint filename_len = strlen(filename);
  uint ext_len = extension ? strlen(extension) : 0;
  uint path_len = dirname_len + filename_len + ext_len + 3;
  char *path = static_cast<char*>(my_malloc(path_len, MYF(0)));
  if (path) {
    if (extension) {
      ut_snprintf(path, path_len, "%s/%s.%s",
          dirname, filename, extension);
    } else {
      ut_snprintf(path, path_len, "%s/%s",
          dirname, filename);
    }
  }
  return path;
}

static char* remove_dir_slashes(const char *filename)
{
  char *str = my_strdup(filename, MYF(0));
  if (str) {
    for (char *p = str; *p; p++) {
      if (*p == '.' || *p == '/')
        *p = '_';
    }
  }
  return str;
}

static longlong chunk_size_for_slow_rm()
{
  return (longlong)srv_slowrm_speed_mbps * megabyte;
}

int slowfileremove(const char *filename)
{
  struct stat statinfo;
  char ext[32];
  int ret = 0;
  char *fname = NULL;
  char *filetoremove = NULL;

  if (!srv_slowrm_speed_mbps) {
    return unlink(filename);
  }

  ret = stat(filename, &statinfo);
  // if file is not regular or smaller then chunk size, just delete it
  if (ret || !S_ISREG(statinfo.st_mode)
      || statinfo.st_size < chunk_size_for_slow_rm()) {
    return unlink(filename);
  }

  // create slowrm_dir directory if it does not exist
  if (!os_file_create_subdirs_if_needed(slowrm_dir)) {
    ib_logf(IB_LOG_LEVEL_ERROR,
        "slowfileremove could not create directory: '%s'",
        slowrm_dir);
    return unlink(filename);
  }

  ret = -1;
  ut_snprintf(ext, sizeof(ext), "%ld", (long int)time(NULL));
  // files submitted for removal may contain directory name
  // convert path to file into simple filename
  // "./test/t1.ibd" -> "__test_t1_ibd"
  fname = remove_dir_slashes(filename);
  if (fname) {
    filetoremove = make_path(slowrm_dir, fname, ext);
    if (filetoremove) {
      // move file to slowrm_dir
      ret = rename(filename, filetoremove);
      if (ret) {
        char *reason = strerror(errno);
        ib_logf(IB_LOG_LEVEL_ERROR,
            "slowfileremove could not move file:"
            "'%s' to '%s'\nreason '%s'",
            filename, filetoremove, reason);
      }
      my_free(filetoremove);
    }
    my_free(fname);
  }
  // if rename failed fall back to unlink
  return ret ? unlink(filename) : 0;
}

static void slowrm(const char *path, off_t size)
{
  int fd = open(path, O_RDWR);
  if (fd < 0)
    return;
  while(size > chunk_size_for_slow_rm()
      && srv_shutdown_state == SRV_SHUTDOWN_NONE) {
    // convert bytes per seconds into bytes per 0.1 second
    // max speed 100000 (100 Gbps)
    // so (speed * 1000000 / 10) cannot produce ulonglong overflow
    longlong chunk_size = chunk_size_for_slow_rm() / 10;
    if (!chunk_size) // srv_slowrm_speed_mbps is 0
      break;
    if (chunk_size > size)
      size = 0;
    else
      size -= chunk_size;
    if (ftruncate(fd, size) < 0)
      break;
    fsync(fd);
    usleep(100000); // 0.1 second
  }
  // if we finished truncating earlier because of server shutdown
  // and file size <= chunk size
  // then delete it
  if (size <= chunk_size_for_slow_rm())
    unlink(path);
  close(fd);
}

static void remove_all_files_in(const char *slowrm_dirname)
{
  os_file_dir_t dirp = os_file_opendir(slowrm_dirname, false);
  if (dirp == NULL) {
      return;
  }
  while (srv_shutdown_state == SRV_SHUTDOWN_NONE) {
    os_file_stat_t finfo;
    int rc = os_file_readdir_next_file(slowrm_dirname, dirp, &finfo);
    if (rc)
      break;
    char *path = make_path(slowrm_dirname, finfo.name, NULL);
    if (path) {
      if (finfo.type == OS_FILE_TYPE_FILE)
        slowrm(path, finfo.size);
      my_free(path);
    }
  }
  os_file_closedir(dirp);
}

static os_event_t event = NULL;

void wake_up_file_slow_removal_thread()
{
  if (event)
    os_event_set(event);
}

// background process which slowly truncates files in slowrm_dir
extern "C" UNIV_INTERN
os_thread_ret_t
DECLARE_THREAD(big_file_slow_removal_thread)(
/*=====================================*/
  void* arg MY_ATTRIBUTE((unused)))
{
  ib_int64_t  sig_count = 0;
  os_event_t  event = os_event_create();

#ifdef UNIV_PFS_THREAD
  pfs_register_thread(srv_slowrm_thread_key);
#endif /* UNIV_PFS_THREAD */

  do {

    /* sleep thread_sleep_interval microseconds*/
    os_event_wait_time_low(event, thread_sleep_interval, sig_count);
    sig_count = os_event_reset(event);

    if (srv_shutdown_state != SRV_SHUTDOWN_NONE) {
      break;
    }

    remove_all_files_in(slowrm_dir);

  } while (srv_shutdown_state == SRV_SHUTDOWN_NONE);

  os_thread_exit(NULL);
  OS_THREAD_DUMMY_RETURN;
}
