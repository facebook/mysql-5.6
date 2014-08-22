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


/* This file defines all json string functions (using FBSON library) */


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
 * which is the encoding of json object in FBSON
 */
#include "../fbson/FbsonJsonParser.h"
#include "../fbson/FbsonUtil.h"

static String*
ValueToString(fbson::FbsonValue *pval, String &str, const CHARSET_INFO *cs)
{
  String *res = &str;

  switch (pval->type())
  {
  case fbson::FbsonType::T_False:
    {
      res->set_int(0, true /*unsigned_flag*/, cs);
      break;
    }
  case fbson::FbsonType::T_True:
    {
      res->set_int(1, true /*unsigned_flag*/, cs);
      break;
    }
  case fbson::FbsonType::T_Object:
  case fbson::FbsonType::T_Array:
    {
      fbson::FbsonToJson tojson;
      std::string json = tojson.json(pval);
      res->copy(json.c_str(), json.size(), cs);
      break;
    }
  case fbson::FbsonType::T_String:
    {
      fbson::StringVal *str_val = (fbson::StringVal *)pval;
      res->copy(str_val->getBlob(), str_val->getBlobLen(), cs);
      break;
    }
  case fbson::FbsonType::T_Int8:
    {
      res->set_int(((fbson::Int8Val*)pval)->val(), false, cs);
      break;
    }
  case fbson::FbsonType::T_Int16:
    {
      res->set_int(((fbson::Int16Val*)pval)->val(), false, cs);
      break;
    }
  case fbson::FbsonType::T_Int32:
    {
      res->set_int(((fbson::Int32Val*)pval)->val(), false, cs);
      break;
    }
  case fbson::FbsonType::T_Int64:
    {
      res->set_int(((fbson::Int64Val*)pval)->val(), false, cs);
      break;
    }
  case fbson::FbsonType::T_Double:
    {
      res->set_real(((fbson::DoubleVal*)pval)->val(), NOT_FIXED_DEC, cs);
      break;
    }
  case fbson::FbsonType::T_Null:
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
    fbson::FbsonJsonParser parser;
    if (parser.parse(json->c_ptr_safe()))
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
    fbson::FbsonJsonParser parser;
    if (parser.parse(pstr->c_ptr_safe()))
    {
      fbson::FbsonOutStream *out = parser.getWriter().getOutput();
      fbson::FbsonValue *pval = fbson::FbsonDocument::createValue(
          out->getBuffer(), out->getSize());
      DBUG_ASSERT(pval);

      unsigned i = 1;
      for (; i < arg_count && pval && pstr; ++i)
      {
        if (pval->isObject())
        {
          if ( (pstr = args[i]->val_str(str)) )
            pval = ((fbson::ObjectVal*)pval)->find(pstr->c_ptr_safe());
        }
        else if (pval->isArray())
        {
          if ( (pstr = args[i]->val_str(str)) )
          {
            // array index parameter is 0-based
            char *end = nullptr;
            int index = strtol(pstr->c_ptr_safe(), &end, 0);
            if (end && !*end)
              pval = ((fbson::ArrayVal*)pval)->get(index);
            else
              pval = nullptr;
          }
        }
        else
          pval = nullptr;
      }

      if (pval && pstr)
        res = ValueToString(pval,*str,collation.collation);
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
    fbson::FbsonJsonParser parser;
    if (parser.parse(pstr->c_ptr_safe()))
    {
      fbson::FbsonOutStream *out = parser.getWriter().getOutput();
      fbson::FbsonValue *pval = fbson::FbsonDocument::createValue(
          out->getBuffer(), out->getSize());
      DBUG_ASSERT(pval);

      unsigned i = 1;
      for (; i < arg_count && pval && pstr; ++i)
      {
        if (pval->isObject())
        {
          if ( (pstr = args[i]->val_str(&buffer)) )
            pval = ((fbson::ObjectVal*)pval)->find(pstr->c_ptr_safe());
        }
        else if (pval->isArray())
        {
          if ( (pstr = args[i]->val_str(&buffer)) )
          {
            // array index parameter is 0-based
            char *end = nullptr;
            int index = strtol(pstr->c_ptr_safe(), &end, 0);
            if (end && !*end)
              pval = ((fbson::ArrayVal*)pval)->get(index);
            else
              pval = nullptr;
          }
        }
        else
          pval = nullptr;
      }

      if (pval && pstr)
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
    fbson::FbsonJsonParser parser;
    if (parser.parse(pstr->c_ptr_safe()))
    {
      fbson::FbsonOutStream *out = parser.getWriter().getOutput();
      fbson::FbsonValue *pval = fbson::FbsonDocument::createValue(
          out->getBuffer(), out->getSize());
      DBUG_ASSERT(pval);

      if (pval->isArray())
        res = ((fbson::ArrayVal*)pval)->numElem();
      else
        my_error(ER_INVALID_JSON_ARRAY, MYF(0), pstr->c_ptr_safe());
    }
    else
      my_error(ER_INVALID_JSON, MYF(0), pstr->c_ptr_safe());
  }

  return res;
}
