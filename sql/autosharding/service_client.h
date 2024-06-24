// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

using ShardingKeyColumn = std::variant<int64_t, std::string_view, std::string>;

class ShardingKey {
 public:
  explicit ShardingKey(std::vector<ShardingKeyColumn> &&shardingKeyColumns)
      : shardingKey(shardingKeyColumns) {}
  explicit ShardingKey(
      std::initializer_list<ShardingKeyColumn> shardingKeyColumns)
      : shardingKey(shardingKeyColumns) {}

  bool operator==(const ShardingKey &other) const {
    return shardingKey == other.shardingKey;
  }

  std::vector<ShardingKeyColumn> shardingKey;
};

class IAutoShardingServiceClient {
 public:
  virtual ~IAutoShardingServiceClient() = default;

  virtual int64_t hash(ShardingKey &&key) const = 0;
};

std::unique_ptr<IAutoShardingServiceClient> create_autosharding_service_client(
    const std::string_view &shardmap);

using create_autosharding_service_client_t =
    std::unique_ptr<IAutoShardingServiceClient>(
        const std::string_view &shardmap);

void set_create_autosharding_service_client_callback(
    create_autosharding_service_client_t callback);
