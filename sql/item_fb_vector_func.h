/*
   Copyright (c) 2023, Facebook, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#pragma once

#include "sql/item_func.h"
#include "sql/item_json_func.h"
#include "sql/system_variables.h"

/**
  parent class of vector distance functions
*/
class Item_func_fb_vector_distance : public Item_real_func {
 public:
  Item_func_fb_vector_distance(THD *thd, const POS &pos, PT_item_list *a);

  bool resolve_type(THD *thd) override;

  enum_const_item_cache can_cache_json_arg(Item *arg) override;

  double val_real() override;

  // get the input vector, return false if successful
  bool get_input_vector(std::vector<float> &input_vector);

  // hints passed to storage engine
  ha_rows m_limit = 0;
  enum_fb_vector_search_type m_search_type = FB_VECTOR_SEARCH_KNN_FIRST;
  uint m_nprobe = 0;

 protected:
  /// String used when reading JSON binary values or JSON text values.
  String m_value;
  Fb_vector m_input_vector;
  virtual float compute_distance(const float *v1, const float *v2,
                                 size_t dimension) = 0;
  bool fix_input_vector();
};

/**
  Represents the function FB_VECTOR_L2()
*/
class Item_func_fb_vector_l2 final : public Item_func_fb_vector_distance {
 public:
  Item_func_fb_vector_l2(THD *thd, const POS &pos, PT_item_list *a);

  const char *func_name() const override;
  enum Functype functype() const override;

 protected:
  float compute_distance(const float *v1, const float *v2,
                         size_t dimension) override;
};

/**
  Represents the function FB_VECTOR_IP()
*/
class Item_func_fb_vector_ip final : public Item_func_fb_vector_distance {
 public:
  Item_func_fb_vector_ip(THD *thd, const POS &pos, PT_item_list *a);

  const char *func_name() const override;
  enum Functype functype() const override;

 protected:
  float compute_distance(const float *v1, const float *v2,
                         size_t dimension) override;
};

/**
  Represents the function FB_VECTOR_NORMALIZE_L2()
*/
class Item_func_fb_vector_normalize_l2 final : public Item_json_func {
 public:
  Item_func_fb_vector_normalize_l2(THD *thd, const POS &pos, PT_item_list *a);

  const char *func_name() const override;
  enum Functype functype() const override;

  bool resolve_type(THD *thd) override;

  enum_const_item_cache can_cache_json_arg(Item *arg) override;

  bool val_json(Json_wrapper *wr) override;
};

/**
  Represents the function FB_VECTOR_BLOB_TO_JSON()
*/
class Item_func_fb_vector_blob_to_json final : public Item_json_func {
 public:
  Item_func_fb_vector_blob_to_json(THD *thd, const POS &pos, PT_item_list *a);
  const char *func_name() const override;
  enum Functype functype() const override;
  bool resolve_type(THD *thd) override;
  enum_const_item_cache can_cache_json_arg(Item *arg) override;
  bool val_json(Json_wrapper *wr) override;
};

/**
  Represents the function FB_VECTOR_JSON_TO_BLOB()
*/
class Item_func_fb_vector_json_to_blob final : public Item_str_func {
 public:
  Item_func_fb_vector_json_to_blob(const POS &pos, PT_item_list *a);
  const char *func_name() const override;
  enum Functype functype() const override;
  bool resolve_type(THD *thd) override;
  String *val_str(String *) override;

 private:
  // String used to store val_str() result.
  String m_value;
};

bool parse_fb_vector_from_item(Item **args, uint arg_idx, String &str,
                               const char *func_name, Fb_vector &vector);
