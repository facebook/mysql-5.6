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

/*
 * Converts FbsonValue object to string and write to str
 * Input: pval - FbsonValue object to convert
 *        str - output
 *        cs - character set
 * Output: true - success
 */
static bool
ValueToString(fbson::FbsonValue *pval, String &str, const CHARSET_INFO *cs)
{
  if (!pval)
    return false;

  switch (pval->type())
  {
  case fbson::FbsonType::T_False:
    {
      str.set_int(0, true /*unsigned_flag*/, cs);
      return true;
    }
  case fbson::FbsonType::T_True:
    {
      str.set_int(1, true /*unsigned_flag*/, cs);
      return true;
    }
  case fbson::FbsonType::T_Object:
  case fbson::FbsonType::T_Array:
    {
      fbson::FbsonToJson tojson;
      const char *json = tojson.json(pval);
      str.copy(json, strlen(json), cs);
      return true;
    }
  case fbson::FbsonType::T_String:
    {
      fbson::StringVal *str_val = (fbson::StringVal *)pval;
      str.copy(str_val->getBlob(), str_val->getBlobLen(), cs);
      return true;
    }
  case fbson::FbsonType::T_Int8:
    {
      str.set_int(((fbson::Int8Val*)pval)->val(), false, cs);
      return true;
    }
  case fbson::FbsonType::T_Int16:
    {
      str.set_int(((fbson::Int16Val*)pval)->val(), false, cs);
      return true;
    }
  case fbson::FbsonType::T_Int32:
    {
      str.set_int(((fbson::Int32Val*)pval)->val(), false, cs);
      return true;
    }
  case fbson::FbsonType::T_Int64:
    {
      str.set_int(((fbson::Int64Val*)pval)->val(), false, cs);
      return true;
    }
  case fbson::FbsonType::T_Double:
    {
      str.set_real(((fbson::DoubleVal*)pval)->val(), NOT_FIXED_DEC, cs);
      return true;
    }
  case fbson::FbsonType::T_Null:
  default:
      return false;
  }

  return false;
}

/*
 * Checks if item is a document
 * Input: item - input item
 *        json - output, json string or fbson blob
 * Output: true - output is fbson blob (doc type)
 *         false - output is json string
 */
static bool is_doc_type(Item *item, String *&json, String *buffer)
{
  bool doc_type = (item->field_type() == MYSQL_TYPE_DOCUMENT);
  if (doc_type)
    json = item->val_doc(buffer); // this is an FBSON blob
  else
    json = item->val_str(buffer);

  return doc_type;
}

/*
 * Parses JSON c_str into FBSON value object
 * Input: c_str - JSON string (null terminated)
 *        os - output stream storing FBSON packed bytes
 * Output: FbsonValue object.
 *         NULL if JSON is invalid
 */
static fbson::FbsonValue *get_fbson_val(const char *c_str,
                                        fbson::FbsonOutStream &os)
{
  // try parsing input as JSON
  fbson::FbsonValue *pval = nullptr;
  fbson::FbsonJsonParser parser(os);
  if (parser.parse(c_str))
  {
    pval = fbson::FbsonDocument::createValue(
        os.getBuffer(), os.getSize());
    DBUG_ASSERT(pval);
  }
  else
    my_error(ER_INVALID_JSON, MYF(0), c_str);

  return pval;
}

/*
 * Gets FBSON value object directly from str (FBSON blob)
 * Input: str - FBSON blob
 *        len - blob length
 * Output: FbsonValue object.
 */
static fbson::FbsonValue *get_fbson_val(const char *str, unsigned int len)
{
  // this is an FBSON blob
  fbson::FbsonValue *pval = fbson::FbsonDocument::createValue(str, len);
  DBUG_ASSERT(pval);

  return pval;
}

/*
 * Item_func_json_valid
 */

bool Item_func_json_valid::val_bool()
{
  DBUG_ASSERT(fixed);

  null_value = 0;
  String buffer;
  String *json = nullptr;

  bool doc_type = is_doc_type(args[0], json, &buffer);

  if (json)
  {
    if (doc_type)
      return true; // FBSON blob

    fbson::FbsonJsonParser parser;
    return parser.parse(json->c_ptr_safe());
  }

  null_value = 1;
  return false;
}

longlong Item_func_json_valid::val_int()
{
  return (val_bool() ? 1 : 0);
}

/*
 * Extracts key path (stored in args) from pval
 * Input: args - path arguments
 *        arg_count - # of path elements
 *        pval - FBSON value object to extract from
 *        str - string buffer
 * Output: FbsonValue object pointed by key path.
 *         NULL if path is invalid
 */
static fbson::FbsonValue*
json_extract_helper(Item **args,
                    uint arg_count,
                    fbson::FbsonValue *pval, /* in: fbson value object */
                    String *str) /* out: string buffer */
{
  for (unsigned i = 1; i < arg_count && pval; ++i)
  {
    String *pstr;
    if (pval->isObject())
    {
      if ( (pstr = args[i]->val_str(str)) )
        pval = ((fbson::ObjectVal*)pval)->find(pstr->c_ptr_safe());
      else
        pval = nullptr;
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
      else
        pval = nullptr;
    }
    else
      pval = nullptr;
  }

  return pval;
}

/*
 * Item_func_json_extract
 */

String *Item_func_json_extract::val_str(String *str)
{
  DBUG_ASSERT(fixed);

  null_value = 0;
  String *pstr = nullptr;

  bool doc_type = is_doc_type(args[0], pstr, str);
  if (pstr)
  {
    fbson::FbsonValue *pval = nullptr;
    if (doc_type)
    {
      pval = get_fbson_val(pstr->ptr(), pstr->length());
      pval = json_extract_helper(args, arg_count, pval, str);
      if (ValueToString(pval,*str,collation.collation))
        return str;
    }
    else
    {
      fbson::FbsonOutStream os;
      pval = get_fbson_val(pstr->c_ptr_safe(), os);
      pval = json_extract_helper(args, arg_count, pval, str);
      if (ValueToString(pval,*str,collation.collation))
        return str;
    }
  }

  null_value = 1;
  return nullptr;
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

  null_value = 0;
  String buffer;
  String *pstr = nullptr;

  bool doc_type = is_doc_type(args[0], pstr, &buffer);
  if (pstr)
  {
    fbson::FbsonValue *pval = nullptr;
    if (doc_type)
    {
      pval = get_fbson_val(pstr->ptr(), pstr->length());
      return json_extract_helper(args, arg_count, pval, &buffer) != nullptr;
    }
    else
    {
      fbson::FbsonOutStream os;
      pval = get_fbson_val(pstr->c_ptr_safe(), os);
      return json_extract_helper(args, arg_count, pval, &buffer) != nullptr;
    }
  }

  null_value = 1;
  return false;
}

longlong Item_func_json_contains_key::val_int()
{
  return (val_bool() ? 1 : 0);
}

/*
 * Gets array length from FbsonValue object
 * Input: pval - FbsonValue object (array)
 *        json - original JSON string
 * Output: number of array elements (array length)
 *         0 if pval is not an array object
 */
static longlong
json_array_length_helper(fbson::FbsonValue *pval, const char *json)
{
  if (pval && pval->isArray())
    return ((fbson::ArrayVal*)pval)->numElem();
  else
    my_error(ER_INVALID_JSON_ARRAY, MYF(0), json);

  return 0;
}

/*
 * Item_func_json_array_length
 */

longlong Item_func_json_array_length::val_int()
{
  DBUG_ASSERT(fixed);

  null_value = 0;
  String buffer;
  String *pstr = nullptr;

  bool doc_type = is_doc_type(args[0], pstr, &buffer);

  if (pstr)
  {
    fbson::FbsonValue *pval = nullptr;
    if (doc_type)
    {
      pval = get_fbson_val(pstr->ptr(), pstr->length());
      return json_array_length_helper(pval, pstr->c_ptr_safe());
    }
    else
    {
      fbson::FbsonOutStream os;
      pval = get_fbson_val(pstr->c_ptr_safe(), os);
      return json_array_length_helper(pval, pstr->c_ptr_safe());
    }
  }

  null_value = 1;
  return 0;
}
