// Copyright 2004-present Facebook. All Rights Reserved.
#include <string>
#include "rocksdb/env.h"
/*
  Fetch rockdb::Env according to given uri, also output relative path for given
  uri SYNOPSIS

  DESCRIPTION

  RETURN VALUE
    pointer	Pointer to valid rocksdb::Env
    0		Error
*/
rocksdb::Env *get_wsenv_from_uri(const std::string &wsenv_uri,
                                 std::string *relative_file);

/*
  Fetch rocksdb::EnvOptions instance
  RETURN VALUE
    pointer	Pointer to valid rocksdb::EnvOptions
    nullptr		Error
 *
*/
rocksdb::EnvOptions *get_wsenv_options();
