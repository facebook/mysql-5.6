// Copyright 2004-present Facebook. All Rights Reserved.

#include "my_sys.h"

/**
  Checks whether the calling process can access the file
  path in warm storage

   @param uri Full URI Path of file in warm storage
   @param amode F_OK | WS_ACCESS_MODE

   @retval 0 if successful
   @retval -1 in case of errors
*/
int my_ws_access(const std::string &uri, int amode);

/**
  Close a file on warm storage.

  @param file   File descriptor

  @retval 0 if successful
  @retval -1 in case of errors
*/
int my_ws_close(File fd);

/**
   Create a new file on warm storage.

   @param uri           Uri Path-name of warm storage file
   @param access_flags  RandomAccessFile & SequentialFile & WritableFile &
   RandomRWFile

   @retval File descriptor
   @retval -1 in case of errors.
*/
File my_ws_create(const std::string &uri, int access_flags);

/**
   Sync both data and metadata to warm storage

   @param file   File descriptor

   @retval 0 if successful
   @retval -1 in case of errors
*/
int my_ws_fsync(File fd);

/**
   Open a new file on warm storage.

   @param uri  uri Path-name of file
   @param access_flags  WS_SEQWRT & WS_RNDRD

   @retval File descriptor
   @retval -1 in case of errors.
*/
File my_ws_open(const std::string &uri, int access_flags);

/**
  Read a chunk of bytes from a file on warm storage

  returns
    On success, the number of bytes read.
    On failure, -1
*/
size_t my_ws_read(File file, uchar *buffer, size_t count);

/**
  Emulate file seek on warm storage

  Seek to the current position if it supports it.
  Otherwise it'll fail if the operation is unsupported.
*/
my_off_t my_ws_seek(File fd, my_off_t pos, int whence);

/**
  Sync only data to warm storage

   @param file   File descriptor

   @retval 0 if successful
   @retval -1 in case of errors
*/
int my_ws_sync(File fd);

/**
  Tell current position of file on warm storage

   @param file   File descriptor

   @retval The current position in the file if successful
   @retval -1 in case of errors
*/
my_off_t my_ws_tell(File fd);

/**
  Write a chunk of bytes to a file on warm storage

  returns
    On success, the number of bytes written.
    On failure, -1
*/
size_t my_ws_write(File fd, const uchar *buffer, size_t Count);

/**
  Retrieve filename(URI) for specified fd

  returns
    On success, WSEnv URI
    On failure, "Unknown WS FD"
*/
const char *my_ws_filename(File fd);
