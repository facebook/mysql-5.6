# Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
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

# Some notes:
# - DECLARE ... CONDITION must be before any DECLARE ... HANDLER
# - DECLARE ... HANDLER   must be before any statements like INSERT/CALL etc.
# - If a handler refers to a condition than this condition must be already defined.
#
# We avoid using _digit for possible signal values because that may conflict 
# with some error codes defined in the MySQL executor. Specifically, signal 5 
# would be interpreted as an Out Of Memory error by the RQG framework. 
# Instead we use a set of discrete values with no apparent conflicts, i.e. 
# outside the range of MySQL error codes, see rule "number".

query_init:
	{ $width = 100 ; return undef };

query:
	set_variable      | update            |
	create_procedure  | create_procedure  | create_function | create_function |
	create_procedure1 | create_procedure1 |
	drop_procedure    | drop_function     |
	drop_procedure1   | drop_function     |
	call_procedure    | call_procedure    | call_procedure   | call_procedure |
	SET @@max_sp_recursion_depth = _digit ;

create_function:
	CREATE FUNCTION function_name ( arg1 variable_type, arg2 variable_type , arg3 variable_type) RETURNS variable_type BEGIN procedure ; RETURN value ; END ;
drop_function:
	DROP FUNCTION function_name ;
function_name:
	f1 | f2 | f3 ;

create_procedure:
	CREATE PROCEDURE procedure_name ( arg1 variable_type , arg2 variable_type , arg3 variable_type ) BEGIN procedure ; END ;
procedure_name:
	p1 | p2 | p3 ;
procedure:
	declare_variable ; declare_condition ; declare_handler ; procedure_statement ; procedure_statement ; procedure_statement ; procedure_statement ; procedure_statement ;
drop_procedure:
	DROP PROCEDURE procedure_name ;
call_procedure:
	CALL procedure_name ( value , value , value ) |
	CALL procedure_name1 ()                       ;
procedure_statement:
	set_variable    |
	signal_resignal |
	if              |
	call_procedure  |
	update          ;

create_procedure1:
	# Some brute force generated nesting.
	# Note:
	# 1. $begin_count
	#    The number of "BEGIN" and "END" must be equal. Therefore we count the number of "BEGIN" without closing "END" in $begin_count.
	#    add_left:   Add one or two "BEGIN"
	#    add_right:  Add sometimes (In 50 % of all cases where $begin_count > 1) an "END"
	#    create_end: Add all missing "END" so that we do not get a syntax error.
	# 2. $cond_count
	#    The number of defined conditions.
	{ $begin_count = 1 ; $cond_count = 0 ; return undef } create_begin BEGIN                                       middle ;                                                 create_end |
	{ $begin_count = 0 ; $cond_count = 0 ; return undef } create_begin                                  add_left ; middle ; add_right ;                                     create_end |
	{ $begin_count = 0 ; $cond_count = 0 ; return undef } create_begin                       add_left ; add_left ; middle ; add_right ; add_right ;                         create_end |
	{ $begin_count = 0 ; $cond_count = 0 ; return undef } create_begin            add_left ; add_left ; add_left ; middle ; add_right ; add_right ; add_right ;             create_end |
	{ $begin_count = 0 ; $cond_count = 0 ; return undef } create_begin add_left ; add_left ; add_left ; add_left ; middle ; add_right ; add_right ; add_right ; add_right ; create_end ;
create_begin:
	CREATE PROCEDURE procedure_name1 () ;
procedure_name1:
	{ $procedure_name = 'p1_'.$prng->int(1,$width) } ;
add_left:
	{ $begin_count++ ;                  return undef } BEGIN some_statement                                                                                      |
	{ $begin_count++ ; $begin_count++ ; return undef } BEGIN some_declare_condition DECLARE handler_type HANDLER FOR handler_condition_list BEGIN handler_action ;
middle:
	some_statement ;
add_right:
	some_statement { if ( 0 == $prng->int(0,1) && $begin_count > 1 ) { $begin_count-- ; return "; END " } else { return undef } } ;
create_end:
	some_statement { $val = '' ; while ($begin_count > 0) { $begin_count-- ; $val = $val.'; END' } ; return $val } ;
some_statement:
	# No error, no warning
	SELECT 1                                      |
	signal                                        |
	call_procedure                                |
	# Get an error (table does not exist)
	SELECT COUNT(*) FROM t_not_exists             |
	# Get a warning (truncation)
	SELECT CAST('ABC' AS CHAR(2))                 |
	# Row not found
	SELECT * FROM (SELECT 1 AS f1) A WHERE f1 = 0 ;
some_declare_condition:
	 | declare_condition1 ; | declare_condition1 ; declare_condition1 ; ;
declare_condition1:
	{ $cond_count++ ; return undef } DECLARE condition_name_create CONDITION FOR condition_value ;
condition_name_create:
	# 10 % of all cases the first condition name we ever create condition.
	# This can end up in
	# - there is nowhere a condition with this name within the already defined part of the procedure
	#   --> success (the condition name gets accepted)
	# - there is somewhere a condition with this name within the already defined part of the procedure
	#   - success (this already existing condition is within another block)
	#   - failure (this already existing condition is within another block)
	{ if ( 10 == $prng->int(1,10) ) { return 'cond1' } else { return 'cond'.$cond_count } } ;
condition_name_use:
	# In case $cond_count == 0 we get an error because the condition does not exist
	{ return 'cond'.$cond_count } ;
handler_type:
	# UNDO is not supported
	# UNDO   |
	CONTINUE |
	EXIT     ;
signal:
	SIGNAL   signal_condition_value optional_signal_information ;
resignal:
	RESIGNAL                        optional_signal_information |
	RESIGNAL signal_condition_value optional_signal_information ;

optional_signal_information:
	 |
	SET signal_information_list ;

handler_action:
	some_statement ; resignal |
	some_statement ;
drop_procedure1:
	DROP PROCEDURE procedure_name1 ;

# This is currently unused and might be removed later.
# declaration:
# 	declare_handler   |
# 	declare_condition |
# 	declare_variable  ;

update:
	UPDATE _table SET _field = value ;

if:
	IF variable_name = value THEN signal_resignal ; ELSEIF variable_name = value THEN signal_resignal ; ELSE signal_resignal ; END IF ;

declare_variable:
	DECLARE variable_name variable_type default_value;

set_variable:
	SET at_variable_name = value ;

value:
	# We avoid using _digit here due to conflicts with other error codes, 
	# see comment in top of this file for details.
	CONVERT( _varchar(512) USING some_charset )            |
	_varchar(512)						|	
	number |
	at_variable_name                                       |
	function_name ( _english , number , at_variable_name ) |
	RPAD( _letter , some_size , 'A123456789' )             ;

some_size:
	17 | 17 | 17 | 17 | 17 | 17 | 17 | 17 | 17 | 17 | 17 | 17 | 17 | 17 | 17 | 17 |
	32  + 1 | 32  + 1 | 32  + 1 | 32  + 1 | 32  + 1 | 32  + 1 | 32  + 1 | 32  + 1 |
	64  + 1 | 64  + 1 | 64  + 1 | 64  + 1 |
	256 + 1 | 256 + 1 |
	256 * 256 + 1 ;

some_charset:
	UTF8 | BINARY | LATIN1 ;

variable_name:
	var1 ;
#| var2 | var3 ;

at_variable_name:
	@var1 ;
#| @var2 | @var3 ;

variable_type:
	INTEGER | VARCHAR(32) ;

default_value:
	# If the default is NULL than we assign in some situation NULL to
	# condition_information_item --> error.
	# We avoid using _digit here due to conflicts with other error codes, 
	# see comment in top of this file for details.
	 | DEFAULT _english | DEFAULT number ;

declare_condition:
	DECLARE condition_name CONDITION FOR condition_value ;

condition_name:
	cond1 ;
#| cond2 | cond3 ;

signal_resignal:
	signal   |
	resignal ;

signal_information_list:
	signal_information |
	signal_information , signal_information ;

value_keyword:
	| VALUE ;

signal_information:
	condition_information_item = simple_value_specification ;

declare_handler:
	DECLARE handler_type HANDLER FOR handler_condition_list procedure_statement ;

sql_state_value:
	SQLSTATE value_keyword sqlstate_value ;

signal_condition_value:
# Notes:
# - MySQL error codes like 1022 (ER_DUP_KEY) are not supported by signal/resignal
# - SIGNAL/RESIGNAL can only use a CONDITION defined with SQLSTATE
#   Therefore we use condition_name less frequent.
	sql_state_value | sql_state_value | sql_state_value | sql_state_value |
	condition_name  ;

handler_condition_value:
	signal_condition_value |
	SQLWARNING             |
	NOT FOUND              |
	SQLEXCEPTION           |
	mysql_error_code       ;
handler_condition_list:
# This is used in DECLARE ... HANDLER ...
	handler_condition_value                           |
	handler_condition_value , handler_condition_value ;

condition_value:
# This is used in DECLARE ... CONDITION ....
# SIGNAL/RESIGNAL can only use a CONDITION defined with SQLSTATE.
# Therefore we decrease the likelihood of mysql_error_code.
	sql_state_value  | sql_state_value |
	sql_state_value  | sql_state_value |
	mysql_error_code ;
	
condition_information_item:
	CLASS_ORIGIN
	| SUBCLASS_ORIGIN
	| CONSTRAINT_CATALOG
	| CONSTRAINT_SCHEMA
	| CONSTRAINT_NAME
	| CATALOG_NAME
	| SCHEMA_NAME
	| TABLE_NAME
	| COLUMN_NAME
	| CURSOR_NAME
	| MESSAGE_TEXT	| MESSAGE_TEXT 	| MESSAGE_TEXT	| MESSAGE_TEXT	| MESSAGE_TEXT
	| MESSAGE_TEXT	| MESSAGE_TEXT 	| MESSAGE_TEXT	| MESSAGE_TEXT	| MESSAGE_TEXT
	| MESSAGE_TEXT	| MESSAGE_TEXT 	| MESSAGE_TEXT 	| MESSAGE_TEXT 	| MESSAGE_TEXT
	| MYSQL_ERRNO 	| MYSQL_ERRNO 	| MYSQL_ERRNO 	| MYSQL_ERRNO	| MYSQL_ERRNO
;

simple_value_specification:
	# We avoid using _digit here due to conflicts with other error codes, 
	# see comment in top of this file for details.
	_varchar(512)	|
	_english         |
	number |
	variable_name    |
	at_variable_name ;
	
mysql_error_code:
	# 0 causes an error in the context where this grammar item is used.
	# Therefore it should be rare.
	0 |
	1022 | 1022 | # ER_DUP_KEY
	1062 | 1062 | # ER_DUP_ENTRY
	1106 | 1106 | # ER_UNKNOWN_PROCEDURE
	1305 | 1305 | # ER_SP_DOES_NOT_EXIST
	1146 | 1146 | # ER_NO_SUCH_TABLE
	1319 | 1319   # ER_SP_COND_MISMATCH
;

sqlstate_value:
	'42000'	| # ER_UNKNOWN_PROCEDURE,ER_SP_DOES_NOT_EXIST,ER_UNKNOWN_PROCEDURE,...)
	'42S02'	| # ER_NO_SUCH_TABLE
	'HY000'	| # generic
	'23000'  | # duplicate value (ER_DUP_KEY,ER_DUP_ENTRY)
	{ $state_number = 46000 + $prng->int(1,20) ; return "'".$state_number."'" } ;

number:
	# We try to avoid numbers/digits that may be interpreted as actual error codes.
	9000 | 9001 | 9002 | 9003 | 9004 | 9005 | 9006 | 9007 | 9008 | 9009 ;

_table:
	B | C ;
