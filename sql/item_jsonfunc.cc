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
 *        json_text - whether to return json text or native values
 * Output: true - success
 */
static bool
ValueToString(fbson::FbsonValue *pval,
              String &str,
              const CHARSET_INFO *cs,
              bool json_text)
{
  if (!pval)
    return false;

  switch (pval->type())
  {
  case fbson::FbsonType::T_Null:
    {
      if (json_text)
      {
        str.set("null", 4, cs);
        return true;
      }
      else
        return false;
    }
  case fbson::FbsonType::T_False:
    {
      if (json_text)
        str.set("false", 5, cs);
      else
        str.set_int(0, true /*unsigned_flag*/, cs);

      return true;
    }
  case fbson::FbsonType::T_True:
    {
      if (json_text)
        str.set("true", 4, cs);
      else
        str.set_int(1, true /*unsigned_flag*/, cs);

      return true;
    }
  case fbson::FbsonType::T_String:
    {
      if (!json_text)
      {
        // copy the string without double quotes
        fbson::StringVal *str_val = (fbson::StringVal *)pval;
        str.copy(str_val->getBlob(), str_val->getBlobLen(), cs);
        return true;
      }
      // else json_text, fall through
    }
  case fbson::FbsonType::T_Object:
  case fbson::FbsonType::T_Array:
    {
      fbson::FbsonToJson tojson;
      const char *json = tojson.json(pval);
      str.copy(json, strlen(json), cs);
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
  default:
      return false;
  }

  return false;
}

/*
 * Get FBSON value object from item if item is FBSON binary
 * Otherwise, the string is pointed by json
 * Input: item - input item
 * Output: json - a json string or an fbson binary blob
 * Return: FbsonValue object
 *
 * Note: if the item is a document column, the item value is Fbson binary and
 * an FbsonValue object is returned (the fbson binary is stored in the json
 * output param). Otherwise, the item's string value depends on two conditions:
 * (1) whether it is a DOC_EXTRACT_FUNC, and (2) whether
 * use_fbson_output_format is turned on. If both are true, the item value is
 * Fbson binary and FbsonValue object is returned. Otherwise, the JSON string
 * is stored in json (output param) and return value is nullptr.
 */
static fbson::FbsonValue *get_fbson_val(Item *item,
                                        String *&json,
                                        String *buffer)
{
  fbson::FbsonValue *pval = nullptr;
  if (item->field_type() == MYSQL_TYPE_DOCUMENT)
  {
    // item is a document field, and json is an fbson binary
    json = item->val_doc(buffer); // this is an FBSON blob
    if (json)
      pval = fbson::FbsonDocument::createValue(json->ptr(), json->length());
  }
  else
  {
    json = item->val_str(buffer);
    // we check again if the string is actually FBSON value binary.
    // if use_fbson_output_format is true and item is DOC_EXTRACT_FUNC,
    // then json is a fbson binary, and we convert it to FbsonValue object.
    // otherwise, json is a JSON string.
    if (json &&
        current_thd->variables.use_fbson_output_format &&
        item->type() == item->FUNC_ITEM &&
        ((Item_func*)item)->functype() == ((Item_func*)item)->DOC_EXTRACT_FUNC)
      pval = (fbson::FbsonValue*)(json->ptr());
  }

  return pval;
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
 * Item_func_json_valid
 */

bool Item_func_json_valid::val_bool()
{
  DBUG_ASSERT(fixed);

  null_value = 0;
  String buffer;
  String *json = nullptr;

  // we try to get FbsonVal if first input arg is FBSON binary
  // otherwise the input arg string is returned/stored in json
  fbson::FbsonValue *pval = get_fbson_val(args[0], json, &buffer);

  if (json)
  {
    if (pval)
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
 * Output: FbsonValue object pointed by key path.
 *         NULL if path is invalid
 */
static fbson::FbsonValue*
json_extract_helper(Item **args,
                    uint arg_count,
                    fbson::FbsonValue *pval) /* in: fbson value object */
{
  String buffer;
  String *pstr;
  for (unsigned i = 1; i < arg_count && pval; ++i)
  {
    if (pval->isObject())
    {
      if ( (pstr = args[i]->val_str(&buffer)) )
        pval = ((fbson::ObjectVal*)pval)->find(pstr->c_ptr_safe());
      else
        pval = nullptr;
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
      else
        pval = nullptr;
    }
    else
      pval = nullptr;
  }

  return pval;
}

String *Item_func_json_extract::intern_val_str(String *str, bool json_text)
{
  DBUG_ASSERT(fixed);

  null_value = 0;
  String *pstr = nullptr;

  // we try to get FbsonVal if first input arg is FBSON binary
  // otherwise the input arg string is returned/stored in pstr
  fbson::FbsonValue *pval = get_fbson_val(args[0], pstr, str);

  if (pstr)
  {
    if (pval)
    {
      pval = json_extract_helper(args, arg_count, pval);
      if (pval && current_thd->variables.use_fbson_output_format)
      {
        // if we output FBSON, set the returning str to the underlying buffer
        str->set((char*)pval, pval->numPackedBytes(), collation.collation);
        return str;
      }
      else if (ValueToString(pval,*str,collation.collation, json_text))
        return str;
    }
    else
    {
      fbson::FbsonOutStream os;
      pval = get_fbson_val(pstr->c_ptr_safe(), os);
      pval = json_extract_helper(args, arg_count, pval);
      if (pval && current_thd->variables.use_fbson_output_format)
      {
        str->copy((char*)pval, pval->numPackedBytes(), collation.collation);
        return str;
      }
      else if (ValueToString(pval, *str, collation.collation, json_text))
        return str;
    }
  }

  null_value = 1;
  return nullptr;
}

/*
 * Item_func_json_extract
 * The retrurned string format is valid JSON text, such as:
 *   true, false
 *   null
 *   "string"
 *   123, 123.45
 *   {"key":"value"}
 *   [1,2,3]
 *
 * This is useful if we want to get value in JSON format from key path.
 */

String *Item_func_json_extract::val_str(String *str)
{
  return intern_val_str(str, true /* json_text */);
}

void Item_func_json_extract::fix_length_and_dec()
{
  // use the json data size (first arg)
  ulonglong char_length= args[0]->max_char_length();
  fix_char_length_ulonglong(char_length);
}

/*
 * Item_func_json_extract_value
 * The returned string format is raw value, such as:
 *   1 (for true), 0 (for false)
 *   NULL row (for null)
 *   string (no double quotes)
 *   123, 123.45
 *   {"key":"value"}
 *   [1,2,3]
 *
 * This is useful if the value will be directly used in comparsions on string
 * vs. integer, in the WHERE clause.
 */

String *Item_func_json_extract_value::val_str(String *str)
{
  return Item_func_json_extract::intern_val_str(str, false /* json_text */);
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

  // we try to get FbsonVal if first input arg is FBSON binary
  // otherwise the input arg string is returned/stored in pstr
  fbson::FbsonValue *pval = get_fbson_val(args[0], pstr, &buffer);

  if (pstr)
  {
    if (pval)
    {
      return json_extract_helper(args, arg_count, pval) != nullptr;
    }
    else
    {
      fbson::FbsonOutStream os;
      pval = get_fbson_val(pstr->c_ptr_safe(), os);
      return json_extract_helper(args, arg_count, pval) != nullptr;
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

  // we try to get FbsonVal if first input arg is FBSON binary
  // otherwise the input arg string is returned/stored in pstr
  fbson::FbsonValue *pval = get_fbson_val(args[0], pstr, &buffer);

  if (pstr)
  {
    if (pval)
    {
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
