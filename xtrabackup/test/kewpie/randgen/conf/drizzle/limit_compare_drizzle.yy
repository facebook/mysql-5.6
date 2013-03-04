# Copyright (C) 2008-2009 Sun Microsystems, Inc. All rights reserved.
# Use is subject to license terms.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301
# USA

query:
	select | select | select | select | select |
	dml | dml | dml | dml | dml |
	transaction |
	select | select | select | select | select |
	dml | dml | dml | dml | dml |
	transaction |
	select | select | select | select | select |
	dml | dml | dml | dml | dml |
	transaction |
	alter 
;

dml:
	update | insert | delete ;

select:
	SELECT * FROM _table where order_by limit;

where:
	|
	WHERE _field sign value |
	WHERE _field BETWEEN value AND value ;
#	WHERE _field IN ( value , value , value , value , value , value ) ;

sign:
	> | < | = | >= | <> | <= | != ;

order_by:
	ORDER BY _field , `pk` ;

limit:
	LIMIT _digit | LIMIT _tinyint_unsigned | LIMIT 65535 ;

insert:
	INSERT INTO _table ( _field , _field ) VALUES ( value , value ) ;

update:
        int_update | char_update ;

int_update:
	UPDATE _table SET int_field = int_value where order_by limit;

char_update:
        UPDATE _table SET char_field = char_value where order_by limit;

delete:
	DELETE FROM _table where order_by LIMIT digit ;

transaction: START TRANSACTION | COMMIT | ROLLBACK ;

alter:
	ALTER ONLINE TABLE _table DROP KEY letter |
	ALTER ONLINE TABLE _table DROP KEY _field |
	ALTER ONLINE TABLE _table ADD KEY letter ( key_field ) |
	ALTER ONLINE TABLE _table ADD KEY letter ( key_field ) ;

value:
	_english | _digit | _quid | _digit ;

int_value:
  _digit | _tinyint_unsigned | 20 | 30 | 50 | 100  ;

small_int_value:
  _digit | 10 | 20 | 30 | 50 | 100 | 250 ; 

char_value:
   _letter | _english | _quid ;

# Use only indexed fields:

_field:
  char_field | int_field ;

key_field:
  char_key_field | int_field ;

char_field:
     `col_char_10` | `col_char_10_key` | `col_char_10_not_null` | `col_char_10_not_null_key` |
     `col_char_1024` | `col_char_1024_key` | `col_char_1024_not_null` | `col_char_1024_not_null_key` |
     `col_text_not_null` | `col_text_not_null_key` | `col_text_key` | `col_text` ;

int_field: 
     `col_int` | `col_int_key` | `col_int_not_null_key` | `col_int_not_null` |
     `col_bigint` | `col_bigint_key` | `col_bigint_not_null` | `col_bigint_not_null_key` ; 

char_key_field:
# we have this rule to apply a length to blob columns used in a key
     `col_char_10` | `col_char_10_key` | `col_char_10_not_null` | `col_char_10_not_null_key` |
     `col_char_1024` | `col_char_1024_key` | `col_char_1024_not_null` | `col_char_1024_not_null_key` |
     `col_text_not_null`(small_int_value) | `col_text_not_null_key`(small_int_value) | `col_text_key`(small_int_value) | `col_text`(small_int_value) ;

     
      
