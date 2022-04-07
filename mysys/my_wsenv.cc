// Copyright 2004-present Facebook. All Rights Reserved.

#include <mutex>
#include <stdexcept>
#include <string>
#include "include/my_dbug.h"
#include "include/my_sys.h"
#include "mysql/psi/mysql_rwlock.h"
#include "mysys_err.h"
#include "rocksdb/env.h"
#include "sql/mysqld.h"

extern mysql_rwlock_t ws_current_fd_lock;

std::once_flag wsenv_inited;

extern "C" {
// Function pointer to create WS Environment
typedef rocksdb::Env *(*func_get_or_create_wsenv)(const char *uri,
                                                  const char *tenant,
                                                  const char *oncall);
// Function pointer to create EnvOptions pointer
// Use EnvOptions pointer instead of EnvOptions to avoid reference error
typedef rocksdb::EnvOptions *(*func_get_or_create_wsenv_options)(void);
}
static func_get_or_create_wsenv func_wsenv;
static func_get_or_create_wsenv_options func_wsenv_options;

static const char *wsenv_folly_singleton_vault_registration_sym =
    "FollySingletonVaultRegistration";
static const char *wsenv_get_or_create_wsenv_sym = "GetOrCreateWSEnvironment";
static const char *wsenv_create_wsenv_options_sym = "CreateDefaultEnvOptions";

/*
 * Initialize variables, register folly singleton vault and fetch 2 more
 * function pointers to create env later
 * init_wsenv() should be only called once
 */
void init_wsenv() {
  mysql_rwlock_init(0, &ws_current_fd_lock);
  // dynamic load wsenv library
  // use RTLD_LOCAL to avoid polluting subsequently objects
  void *handle = dlopen(sql_wsenv_lib_name, RTLD_LAZY | RTLD_LOCAL);
  assert(handle != nullptr);
  DBUG_EXECUTE_IF("simulate_wsenv_dlopen_error", { handle = nullptr; });
  if (!handle) {
    my_error(EE_WSENV_CANT_OPEN_LIBRARY, MYF(0), sql_wsenv_lib_name, errno);
    return;
  }

  // Register folly Singleton Vault
  void (*follySingletonVaultRegistration)(void);
  follySingletonVaultRegistration = (void (*)(void))dlsym(
      handle, wsenv_folly_singleton_vault_registration_sym);
  assert(follySingletonVaultRegistration != nullptr);
  if (!follySingletonVaultRegistration) {
    my_error(EE_WSENV_CANT_FIND_DL_ENTRY, MYF(0),
             wsenv_folly_singleton_vault_registration_sym, sql_wsenv_lib_name);
    return;
  }
  follySingletonVaultRegistration();

  // Fetch GetOrCreateWSEnvironment function pointer
  func_wsenv =
      (func_get_or_create_wsenv)dlsym(handle, wsenv_get_or_create_wsenv_sym);
  assert(func_wsenv != nullptr);
  DBUG_EXECUTE_IF("simulate_wsenv_dlsym_error", { func_wsenv = nullptr; });
  if (!func_wsenv) {
    my_error(EE_WSENV_CANT_FIND_DL_ENTRY, MYF(0), wsenv_get_or_create_wsenv_sym,
             sql_wsenv_lib_name);
    return;
  }

  // Fetch GetOrCreateDefaultEnvOptions function pointer
  func_wsenv_options = (func_get_or_create_wsenv_options)dlsym(
      handle, wsenv_create_wsenv_options_sym);
  assert(func_wsenv_options != nullptr);
  if (!func_wsenv_options) {
    my_error(EE_WSENV_CANT_FIND_DL_ENTRY, MYF(0),
             wsenv_create_wsenv_options_sym, sql_wsenv_lib_name);
    return;
  }
}

/*
  Fetch rockdb::Env according to given uri, also output relative path for given
  uri
*/
rocksdb::Env *get_wsenv_from_uri(const std::string &wsenv_uri,
                                 std::string *relative_path) {
  std::call_once(wsenv_inited, init_wsenv);

  if (func_wsenv_options == nullptr || func_wsenv == nullptr) return nullptr;

  // uri format: {uri_prefix}/{cluster}/{relative_path}
  int uri_prefix_len = strlen(sql_wsenv_uri_prefix);

  // uri should always contain sql_wsenv_uri_prefix
  assert(strncmp(wsenv_uri.c_str(), sql_wsenv_uri_prefix, uri_prefix_len) == 0);

  std::size_t found = wsenv_uri.find_first_of('/', uri_prefix_len);
  assert(found != std::string::npos);
  if (found == std::string::npos) return nullptr;

  *relative_path = wsenv_uri.substr(found + 1);

  // Extract cluster from uri
  std::string wsenv_cluster = wsenv_uri.substr(0, found);
  assert(!wsenv_cluster.empty());
  if (wsenv_cluster.empty()) return nullptr;

  try {
    auto env =
        func_wsenv(wsenv_cluster.c_str(), sql_wsenv_tenant, sql_wsenv_oncall);
    return env;
  } catch (const std::runtime_error &error) {
    my_error(EE_WSENV_RUNTIME_ERROR, MYF(0), error.what());
    return nullptr;
  }
}

/*
  Fetch EnvOptions pointer to avoid unresolved symbol
*/
rocksdb::EnvOptions *get_wsenv_options() {
  if (func_wsenv_options == nullptr) return nullptr;
  return func_wsenv_options();
}
