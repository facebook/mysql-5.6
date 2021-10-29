/* Copyright (c) 2015, 2020, Oracle and/or its affiliates.

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

#ifndef QUERY_ARENA_INCLUDED
#define QUERY_ARENA_INCLUDED

#include "my_sys.h"

class Item;
struct MEM_ROOT;

class Query_arena {
 private:
  /*
    List of items created for this query. Every item adds itself to the list
    on creation (see Item::Item() for details)
  */
  Item *m_item_list;

 public:
  MEM_ROOT *mem_root;  // Pointer to current memroot
  /// To check whether a reprepare operation is active
  bool is_repreparing{false};
  /*
    The states reflects three different life cycles for three
    different types of statements:
    Prepared statement: STMT_INITIALIZED -> STMT_PREPARED -> STMT_EXECUTED.
    Stored procedure:   STMT_INITIALIZED_FOR_SP -> STMT_EXECUTED.
    Other statements:   STMT_REGULAR_EXECUTION never changes.
  */
  enum enum_state {
    STMT_INITIALIZED = 0,
    STMT_INITIALIZED_FOR_SP = 1,
    STMT_PREPARED = 2,
    STMT_REGULAR_EXECUTION = 3,
    STMT_EXECUTED = 4,
    STMT_ERROR = -1
  };

  /*
    State and state changes in SP:
    1) When state is STMT_INITIALIZED_FOR_SP, objects in the item tree are
       created on the statement memroot. This is enforced through
       ps_arena_holder checking the state.
    2) After the first execute (call p1()), this state should change to
       STMT_EXECUTED. Objects will be created on the execution memroot and will
       be destroyed at the end of each execution.
    3) In case an ER_NEED_REPREPARE error occurs, state should be changed to
       STMT_INITIALIZED_FOR_SP and objects will again be created on the
       statement memroot. At the end of this execution, state should change to
       STMT_EXECUTED.
  */
 private:
  enum_state state;

 public:
  Query_arena(MEM_ROOT *mem_root_arg, enum enum_state state_arg)
      : m_item_list(nullptr), mem_root(mem_root_arg), state(state_arg) {}

  /*
    This constructor is used only when Query_arena is created as
    backup storage for another instance of Query_arena.
  */
  Query_arena()
      : m_item_list(nullptr), mem_root(nullptr), state(STMT_INITIALIZED) {}

  virtual ~Query_arena() = default;

  Item *item_list() const { return m_item_list; }
  void reset_item_list() { m_item_list = nullptr; }
  void set_item_list(Item *item) { m_item_list = item; }
  void add_item(Item *item);
  void free_items();
  void set_state(enum_state state_arg) { state = state_arg; }
  enum_state get_state() const { return state; }
  bool is_stmt_prepare() const { return state == STMT_INITIALIZED; }
  bool is_stmt_prepare_or_first_sp_execute() const {
    return (int)state < (int)STMT_PREPARED;
  }
  bool is_stmt_prepare_or_first_stmt_execute() const {
    return (int)state <= (int)STMT_PREPARED;
  }
  /// @returns true if a regular statement, ie not prepared and not stored proc
  bool is_regular() const { return state == STMT_REGULAR_EXECUTION; }

  void *alloc(size_t size) { return mem_root->Alloc(size); }
  void *mem_calloc(size_t size) {
    void *ptr;
    if ((ptr = mem_root->Alloc(size))) memset(ptr, 0, size);
    return ptr;
  }
  template <typename T>
  T *alloc_typed() {
    void *m = alloc(sizeof(T));
    return m == nullptr ? nullptr : new (m) T;
  }
  template <typename T>
  T *memdup_typed(const T *mem) {
    return static_cast<T *>(memdup_root(mem_root, mem, sizeof(T)));
  }
  char *mem_strdup(const char *str) { return strdup_root(mem_root, str); }
  char *strmake(const char *str, size_t size) const {
    return strmake_root(mem_root, str, size);
  }
  LEX_CSTRING strmake(LEX_CSTRING str) {
    LEX_CSTRING ret;
    ret.str = strmake(str.str, str.length);
    ret.length = ret.str ? str.length : 0;
    return ret;
  }
  void *memdup(const void *str, size_t size) {
    return memdup_root(mem_root, str, size);
  }

  /**
    Copies memory-managing members from `set`. No references are kept to it.

    @param set A Query_arena from which members are copied.
  */
  void set_query_arena(const Query_arena &set);

  /**
    Copy the current arena to `backup` and set the current
    arena to match `source`

    @param source A Query_arena from which members are copied.
    @param backup A Query_arena to which members are first saved.
  */
  void swap_query_arena(const Query_arena &source, Query_arena *backup);
};

#endif /* QUERY_ARENA_INCLUDED */
