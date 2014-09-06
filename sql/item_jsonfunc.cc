/*
   Copyright (c) 2014, Facebook. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
*/


/* This file defines all json string functions (using rapidjson) */


/* May include caustic 3rd-party defs. Use early, so it can override nothing. */
#include "my_global.h"

/*
  It is necessary to include set_var.h instead of item.h because there
  are dependencies on include order for set_var.h and item.h. This
  will be resolved later.
*/
#include "sql_class.h"
#include "set_var.h"
#include "mysqld.h"

/*
 * Note we assume the system charset is UTF8,
 * which is the encoding of json object in rapidjson
 */
#include "../rapidjson/document.h"   // rapidjson's DOM-style API
#include "../rapidjson/writer.h"     // for stringify JSON
#include "../rapidjson/stringbuffer.h"

static String*
ValueToString(rapidjson::Value &value, String &str, const CHARSET_INFO *cs)
{
  String *res = &str;

  switch (value.GetType())
  {
  case rapidjson::kFalseType:
    {
      res->set_int(0, true /*unsigned_flag*/, cs);
      break;
    }
  case rapidjson::kTrueType:
    {
      res->set_int(1, true /*unsigned_flag*/, cs);
      break;
    }
  case rapidjson::kObjectType:
  case rapidjson::kArrayType:
    {
      rapidjson::StringBuffer buffer;
      rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
      value.Accept(writer);
      res->copy(buffer.GetString(), buffer.GetSize(), cs);
      break;
    }
  case rapidjson::kStringType:
    {
      res->copy(value.GetString(), value.GetStringLength(), cs);
      break;
    }
  case rapidjson::kNumberType:
    {
			if (value.IsInt())
        res->set_int(value.GetInt(), false, cs);
			else if (value.IsUint())
        res->set_int(value.GetUint(), true, cs);
			else if (value.IsInt64())
        res->set_int(value.GetInt64(), false, cs);
			else if (value.IsUint64())
        res->set_int(value.GetUint64(), true, cs);
			else /* value.IsDouble() */
        res->set_real(value.GetDouble(), NOT_FIXED_DEC, cs);
      break;
    }
  case rapidjson::kNullType:
  default:
      res = nullptr;
  }

  return res;
}

/*
 * Item_func_json_valid
 */

bool Item_func_json_valid::val_bool()
{
  DBUG_ASSERT(fixed);

  String buffer;
  String *json = args[0]->val_str(&buffer);
  null_value= (json ? 0:1);
  if (json)
  {
    rapidjson::Document document;
    if (!document.Parse<0>(json->c_ptr_safe()).HasParseError())
      return true;
  }

  return false;
}

longlong Item_func_json_valid::val_int()
{
  return (val_bool() ? 1 : 0);
}

/*
 * Item_func_json_extract
 */

String *Item_func_json_extract::val_str(String *str)
{
  DBUG_ASSERT(fixed);

  null_value = 0;
  String *res = nullptr;
  String *pstr = args[0]->val_str(str);
  if (pstr)
  {
    rapidjson::Document document;
    if (!document.Parse<0>(pstr->c_ptr_safe()).HasParseError())
    {
      rapidjson::Value &value = document;
      unsigned i = 1;
      for (; i < arg_count; ++i)
      {
        if (value.IsObject())
        {
          rapidjson::Value::Member *member = nullptr;
          if ( (pstr = args[i]->val_str(str)) )
            member = value.FindMember(pstr->c_ptr_safe());

          if (member)
            value = member->value;
          else break;
        }
        else if (value.IsArray())
        {
          // array index parameter is 1-based
          int index = args[i]->val_int();
          if (index > 0 && (unsigned)index <= value.Size())
            value = value[(unsigned)index-1];
          else break;
        }
        else break;
      }
      if (i == arg_count)
        res = ValueToString(value,*str,collation.collation);
    }
    else
      my_error(ER_INVALID_JSON, MYF(0), pstr->c_ptr_safe());
  }

  if (!res)
    null_value = 1;

  return res;
}

void Item_func_json_extract::fix_length_and_dec()
{
  // use the json data size (first arg)
  ulonglong char_length= args[0]->max_char_length();
  fix_char_length_ulonglong(char_length);
}

/*
 * Item_func_json_contains_key
 */

bool Item_func_json_contains_key::val_bool()
{
  DBUG_ASSERT(fixed);

  bool res = false;

  String buffer;
  String *pstr = args[0]->val_str(&buffer);
  null_value = (pstr? 0:1);
  if (pstr)
  {
    rapidjson::Document document;
    if (!document.Parse<0>(pstr->c_ptr_safe()).HasParseError())
    {
      rapidjson::Value &value = document;
      unsigned i = 1;
      for (; i < arg_count; ++i)
      {
        if (value.IsObject())
        {
          rapidjson::Value::Member *member = nullptr;
          if ( (pstr = args[i]->val_str(&buffer)) )
            member = value.FindMember(pstr->c_ptr_safe());

          if (member)
            value = member->value;
          else break;
        }
        else if (value.IsArray())
        {
          // array index parameter is 1-based
          int index = args[i]->val_int();
          if (index > 0 && (unsigned)index <= value.Size())
            value = value[(unsigned)index-1];
          else break;
        }
        else break;
      }
      if (i == arg_count)
        res = true;
    }
    else
      my_error(ER_INVALID_JSON, MYF(0), pstr->c_ptr_safe());
  }

  return res;
}

longlong Item_func_json_contains_key::val_int()
{
  return (val_bool() ? 1 : 0);
}

/*
 * Item_func_json_array_length
 */

longlong Item_func_json_array_length::val_int()
{
  DBUG_ASSERT(fixed);

  longlong res = 0;

  String buffer;
  String *pstr = args[0]->val_str(&buffer);
  null_value = (pstr? 0:1);
  if (pstr)
  {
    rapidjson::Document document;
    if (!document.Parse<0>(pstr->c_ptr_safe()).HasParseError())
    {
      if (document.IsArray())
        res = document.Size();
      else
        my_error(ER_INVALID_JSON_ARRAY, MYF(0), pstr->c_ptr_safe());
    }
    else
      my_error(ER_INVALID_JSON, MYF(0), pstr->c_ptr_safe());
  }

  return res;
}
