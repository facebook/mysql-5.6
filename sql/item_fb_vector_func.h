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

/**
  parent class of vector distance functions
*/
class Item_func_fb_vector_distance : public Item_real_func {
 public:
  Item_func_fb_vector_distance(THD *thd, const POS &pos, PT_item_list *a);

  bool resolve_type(THD *thd) override;

  enum_const_item_cache can_cache_json_arg(Item *arg) override;

  double val_real() override;

 protected:
  /// String used when reading JSON binary values or JSON text values.
  String m_value;

  virtual float compute_distance(float *v1, float *v2, size_t dimension) = 0;
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
  float compute_distance(float *v1, float *v2, size_t dimension) override;
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
  float compute_distance(float *v1, float *v2, size_t dimension) override;
};

/**
  Represents the function FB_VECTOR_COSINE()
*/
class Item_func_fb_vector_cosine final : public Item_func_fb_vector_distance {
 public:
  Item_func_fb_vector_cosine(THD *thd, const POS &pos, PT_item_list *a);

  const char *func_name() const override;
  enum Functype functype() const override;

 protected:
  float compute_distance(float *v1, float *v2, size_t dimension) override;
};

/**
 helper class to parse json to float vector
*/
class Fb_vector {
 public:
  /// the parsed json obj
  Json_wrapper wrapper;
  std::vector<float> data;

  bool set_dimension(size_t n) {
    if (n > data.size()) {
      data.resize(n, 0.0);
      return false;
    }
    // do not allow shrinking the data
    return n < data.size();
  }
};

bool parse_fb_vector_from_item(Item **args, uint arg_idx, String &str,
                               const char *func_name, Fb_vector &vector);
