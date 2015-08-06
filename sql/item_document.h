#ifndef ITEM_DOCUMENT_INCLUDED
#define ITEM_DOCUMENT_INCLUDED

/* Copyright (c) 2015, Facebook. All rights reserved.

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


class Item_func_document :public Item_func
{
private:
  fbson::FbsonJsonParser parser;
  const char *fbson_blob;
  int length;

public:
  Item_func_document(Item *a);
  ~Item_func_document() {}
  Item::Type type() const;
  double val_real();
  longlong val_int();
  fbson::FbsonValue *val_document_value(String *);
  String *val_str(String*);
  my_decimal *val_decimal(my_decimal*);
  void fix_length_and_dec() {}
  const char *func_name() const {return "document";}

  bool get_date(MYSQL_TIME*, uint);
  bool get_time(MYSQL_TIME*);
  Item_result result_type() const { return STRING_RESULT; }
  enum_field_types field_type() const { return MYSQL_TYPE_DOCUMENT_VALUE; }
  type_conversion_status save_in_field(Field *field,
                                       bool no_conversions);

};

#endif /* ITEM_DOCUMENT_INCLUDED */
