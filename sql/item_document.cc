/*
   Copyright (c) 2015, Facebook. All rights reserved.

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

#include "sql_class.h"
#include "set_var.h"
#include "mysqld.h"

Item_func_document::Item_func_document(Item *a) :Item_func(a)
{
  /* Check for valid JSON */
  if (a->type() != STRING_ITEM || !parser.parse(a->str_value.c_ptr()))
    my_error(ER_INVALID_JSON, MYF(0), a->str_value.c_ptr());

  length = parser.getWriter().getOutput()->getSize();
  fbson_blob = parser.getWriter().getOutput()->getBuffer();
}

Item::Type Item_func_document::type() const
{
  return DOCUMENT_ITEM;
}
type_conversion_status Item_func_document::save_in_field(Field *field,
                                                         bool no_conversions){
  fbson::FbsonDocument *doc =
    fbson::FbsonDocument::createDocument(fbson_blob, length);
  if(TYPE_OK !=
     field->store_document(doc,collation.collation)){
    return Item::save_in_field(field, no_conversions);
  }
  return TYPE_OK;
}

double Item_func_document::val_real()
{
  return 0.0;
}

long long Item_func_document::val_int()
{
  return 0ll;
}

String *Item_func_document::val_str(String *str)
{
  fbson::FbsonToJson tojson;
  fbson::FbsonValue *val = fbson::FbsonDocument::createValue(
      fbson_blob, length);

  const char *output = tojson.json(val);
  str->copy(output, strlen(output), str->charset());
  return str;
}

const char *Item_func_document::val_fbson_blob() const
{
  fbson::FbsonValue *val = fbson::FbsonDocument::createValue(
      fbson_blob, length);
  return (const char*) val;
}

my_decimal *Item_func_document::val_decimal(my_decimal *decimal_buffer)
{
  return decimal_buffer;
}

bool Item_func_document::get_date(MYSQL_TIME *ltime, uint fuzzydate)
{
  return false;
}

bool Item_func_document::get_time(MYSQL_TIME *ltime)
{
  return false;
}
