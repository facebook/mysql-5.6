// Copyright 2004-present Facebook. All Rights Reserved.

#include "sql/autosharding/service_client.h"
#include <cstring>

#include "my_sys.h"
#include "mysql_com.h"
#include "mysqld_error.h"
#include "sql/derror.h"

create_autosharding_service_client_t
    *create_autosharding_service_client_callback = nullptr;

void set_create_autosharding_service_client_callback(
    create_autosharding_service_client_t callback) {
  create_autosharding_service_client_callback = callback;
}

std::unique_ptr<IAutoShardingServiceClient> create_autosharding_service_client(
    const std::string_view &shardmap) {
  if (create_autosharding_service_client_callback == nullptr) {
    return nullptr;
  }

  return create_autosharding_service_client_callback(shardmap);
}
