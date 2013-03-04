# Copyright (C) 2008-2010 Sun Microsystems, Inc. All rights reserved.
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

# Grammar for testing DML, DDL, FLUSH, LOCK/UNLOCK, transactions
#
# Created:
#    2009-07 Matthias Leich
#            WL#5004 Comprehensive Locking Stress Test for Azalea
#                    A few grammar rules were taken from other grammar files.
# Last Modifications:
#    2010-05 Matthias Leich
#            Extend grammar for WL#3561 transactional LOCK TABLE
#    2010-06 Matthias Leich
#            - Adjustment to fixed and new bugs for 5.5-m3 mysql-trunk-runtime
#            - Attention: Replication bugs had to be ignored
#
# Attention:
# There are modified grammar rules because of open bugs.
# Please search case insensitive for "disable".
#
# TODO:
#   - Adjust grammar to new open and old fixed bugs
#   - Add TRUNCATE PARTITION and check if we are missing any other related DDL.
#     (Bug#49907 ALTER TABLE ... TRUNCATE PARTITION does not wait for locks on the table)
#     (2010-05 Analysing + should be fixed by patch for
#     (Bug#42643 InnoDB does not support replication of TRUNCATE TABLE)
#     (2010-05 Patch pending)
#   - Add the corresponding DDL when
#        WL#4445 Import/Export tables to/from partitioned tables
#     is ready for testing
#   - Check the impact of the latest modifications (use "used_select" less often) on the issues
#     reported by Philip
#        When using greater values for namespace_width
#        - the database never actually gets the expected number of objects.
#          Even if the DROPs are removed ,then still the database grows very slowly towards the namespace size.
#        - There are a lot of CREATE TABLE table2 SELECT * FROM table1 and similar constructs in order to clone
#          database objects. Unfortunately, at higher namespace values, table1 is not very likely to exist, and
#          therefore table2 is also unlikely to be created.
#   - Add subtest for
#     Bug#48315 Metadata lock is not taken for merged views that use an INFORMATION_SCHEMA table
#     2010-05 Closed but not in tree
#     Simple:
#        Philip: using an I_S in a meaningless subselect would be best, just have
#                ( SELECT user + 0 FROM INFORMATION_SCHEMA.USERS LIMIT 1)
#     Complete:
#        mleich:
#           But IS tables used in VIEWs, SELECT, DELETE/UPDATE subqueries/join,
#           PROCEDURES etc. are complete missing.
#           Could I inject this in a subquery?
#   - Simplify grammar:
#           Namespace concept is good for grammar development, avoiding failing statements,
#           understanding statement logs but bad for grammar simplification speed.
#
# Bug#45225 Locking: hang if drop table with no timeout
#           Reporter , LockTableKiller might help
#     (2010-05 Closed but not in tree)
#
# General architecture rules:
# ---------------------------
# 1. Do not modify the objects created by gendata.pl within this grammar file.
#    Work on copies of these objects instead. Hereby we prevent that we totally run out of tables
#    or rows etc. This minimizes also any locks on the objects created by gendata.pl.
#    Do not
#    - have tables of "special" types (partitioned, view, merge etc.)
#    - variate the storage engine
#    within your object creation grammar file (*.zz).
# 2. Have separated namespaces for objects (tables etc.) with configurable width.
#    - This allows to reduce the likelihood of applying a statement in general or an option to
#      an object which is not allowed. Example: TRUNCATE TABLE <view>
#    - Debugging of grammar and understanding server logs becomes easier if the namespace
#      for an object of some type contains type related strings like "base","temp",.. etc.
#      Example: If there is a
#                  CREATE VIEW <name which does not belong into the VIEW namespace>
#               than something works not like intended.
#    - The configurable namespace width (-> $namespace_width) allows to influence the likelihood
#      that some statement hits an object. This gives some control over how much the system is stressed.
#    - The non default option to put all table related objects (base tables, views, etc.) allows
#      some additional increase of the stress though the likelihood of failing statement raises.
# 3. Distinct between two kinds of object namespaces and treat the corresponding objects different.
#    This is experimental and might be removed in case it does not fulfill the expectations.
#    "Sequence" ("_S"):
#       - statement sequence: CREATE object, fill in content (if applicable), COMMIT, wait some
#         random timespan (-> SLEEP( ... * rand_val * $life_time_unit )) , DROP object
#       - The COMMIT is intentional. It should ensure that the session running the sequence does
#         not hold any locks on the object during the wait phase. I am aware that CREATE ... AS SELECT
#         commits at end, but this might be changed somewhere in future.
#       - the maximum of the random wait timespan is configurable (-> $max_table_life_time).
#       - The object must be stored within a database created with the "_S" property.
#       - No other DDL on this object
#       This should ensure that other sessions have a chance to run several DML statements using this object
#       before it gets dropped. The "Sequence" objects are of nearly no value when running with only one thread.
#    "Normal" ("_N"):
#       - CREATE and DROP for these objects are not within the same sequency.
#       - Any session could run DDL (including DROP, ALTER, REPAIR etc.) on this object.
#       - The object can be stored within any database.
#       - It is assumed that they have a short lifetime.
#       This should ensure that a session running a transaction with DML on this object has a chance to meet
#       an attempt of another session to run DDL (especially ALTER) on this object.
# 4. There is some "generalization" (I am unsure if this is a good understandable term) of variables and
#    a corresponding walk up of values.
#       $database_name_*   --> $database_name
#       $base_table_name_* --> $base_table_name --> $table_name
#       $temp_table_name_* --> $temp_table_name --> $table_name
#       ...
#       $part_table_name_* --> $part_table_name --> $table_name
#    This means:
#    If you run "table_item" which picks a table of random type (base table, temp table ...) and random lifetime
#    and a corresponding database and automatically assigns values to variables ($database_*,$*_table_name_*)
#    where the name cannot be predicted, you can find the generated names at least within
#    $database_name and $table_name .
#    Please be aware that for example a succeeding call of "procedure_item" modifies the content of $database_name.
#
#
# Rules by thumb and experiences (important when extending this grammar file):
# ----------------------------------------------------------------------------
# 1. Any statement sequence has to be in one line.
# 2. Be aware of the dual use of ';'. It separates SQL statements in sequences and closes the definition block
#    of a grammar rules. So any ';' before some '|' has a significant impact.
# 3. Strange not intended effects: '|' or ':' instead of ';' ?
# 4. There is an open RQG problem with SHOW ... LIKE '<grammar rule>'.
# 5. In general: There should be spaces whenever a grammar rule is mentioned in some grammar rule component.
#    Example: "my_table" and  "where" are grammar rules.
#             SELECT MAX(f1) FROM my_table where ;
# 6. If there are needs to write some message into a server log than avoid the use of auxiliary SQL (SELECT <message> etc.).
#    Use something like:
#       /* <your text> */ <SQL statement belonging to the test> ;
#    instead.
# 7. Use uppercase characters for strings and keywords in statements. This avoids any not intended
#    treatment as grammar rule.
# 8. Use the most simple option first in lists. This makes automatic grammar simplification
#    which walks from right to left more efficient. Example:
#    where:
#     	<empty> | WHERE `pk` BETWEEN _digit AND _digit | WHERE function_name_n() = _digit ;
#

# Naming conventions (default)
# ========================================================
#
# Pattern (standard configuration) | Object
# -----------------------------------------
# testdb_*                         | database
# t1_*                             | table/view
# p1_*                             | procedure
# f1_*                             | function
# p1_*                             | procedure
# tr1_*                            | trigger

# End of grammar item name (default) | characteristic
# -------------------------------------------------------
# _S                                 | related to "sequence" object
# _N                                 | related to "normal" object
#
# Within grammar item name  | characteristic
# -----------------------------------------------
# _name                     | name of the object
# _item                     | <schema name> . <name of the object>
# _list                     | either single item (<schema name> . <name of the object>) or comma separated list
#                           | of such items

#
# Missing but not really important improvements:
# - Reduce the amount of cases where "sequence" objects have "normal" objects within their definition.
#   --> views,functions,procedures
# - Reduce the amount of cases where the wrong table types occur within object definitions
#   Example: TABLE for a TRIGGER or VIEW definition. Names of temporary tables could be computed but are not allowed.
#


# Section of easy changeable items with high impact on the test =============================================#
query_init:
	# Variant 1:
	#    Advantage: Less failing (table does not exist ...) statements within the first phase of the test.
	# init_basics : init_namespaces ; event_scheduler_on ; have_some_initial_objects ;
	# Variant 2:
	#    Advantage: Better performance during bug hunt, test simplification etc. because objects are created at
	#               on place (<object>_ddl) only and not also in "have_some_initial_objects".
	init_basics ; init_namespaces ; init_executor_table ;

init_executor_table:
	# This table is used in kill_query_or_session.
	CREATE TABLE IF NOT EXISTS test . executors (id BIGINT, PRIMARY KEY(id)) ENGINE = MEMORY ; INSERT HIGH_PRIORITY IGNORE INTO test.executors SET id = CONNECTION_ID() ; COMMIT ;

init_basics:
	# 1. $life_time_unit = maximum lifetime of a table created within a CREATE, wait, DROP sequence.
	#
	#    A reasonable value is bigger than any "wait for <whatever> lock" timeout.
	#
	#    There are till now not sufficient experiences about the impact of different values on the outcome of the test.
	#
	#    sequence object | lifetime
	#    ------------------------------------------------
	#    database        | 2   * RAND() * $life_time_unit
	#    table (no view) | 1   * RAND() * $life_time_unit
	#    view            | 0.5 * RAND() * $life_time_unit
	#    procedure       | 0.5 * RAND() * $life_time_unit
	#    function        | 0.5 * RAND() * $life_time_unit
	#    trigger         | 0.5 * RAND() * $life_time_unit
	#
	#    A DML statement using SLEEP will use 0.5 * RAND() * $life_time_unit
	#
	#    one_thread_correction will correct $life_time_unit to 0 if we have only one "worker" thread.
	#
	# 2. $namespace_width = Width of a namespace
	#
	#    Smaller numbers cause a
	#    - lower fraction of statements failing because of missing object
	#    - higher fraction of clashes when running with multiple sessions
	#
	# Some notes:
	# - In case of one thread a $life_time_unit <> 0 does not make sense, because there is no parallel
	#   "worker" thread which could do something with the object during the "wait" period.
	{ $life_time_unit = 1 ; $namespace_width = 2 ; if ( $ENV{RQG_THREADS} == 1 ) { $life_time_unit = 0 } ; return undef } avoid_bugs ; nothing_disabled ; system_table_stuff ;

init_namespaces:
	# Please choose between the following alternatives
	# separate_objects         -- no_separate_objects
	# separate_normal_sequence -- no_separate_normal_sequence
	# separate_table_types     -- no_separate_table_types
	# 1. Low amount of failing statements, low risk to run into known not locking related crashes
	separate_objects ; separate_normal_sequence ; separate_table_types ;
	# 2. Higher amount of failing statements, risk to run into known temporary table related crashes
	# separate_objects ; separate_normal_sequence ; no_separate_table_types ;
	# 3. Total chaos
	# High amount of failing statements, risk to run into known temporary table related crashes
	# no_separate_objects ; separate_normal_sequence ; no_separate_table_types ;

separate_table_types:
	# Effect: Distinction between
	#         - base, temporary, merge and partioned tables + views
	#         - tables of any type and functions,procedures,triggers,events
	#         Only statements which are applicable to this type of table will be generated.
	#         Example: ALTER VIEW <existing partitioned table> ... should be not generated.
	# Advantage: Less failing statements, logs are much easier to read
	# Disadvantage: The avoided suitations are not tested.
	{ $base_piece="base" ; $temp_piece="temp" ; $merge_piece="merge" ; $part_piece="part" ; $view_piece="view" ; return undef } ;
no_separate_table_types:
	# Expected impact:
	# - maybe higher load on tables of all types in general (depends on size of namespace)
	# - a significant fraction of statements will fail with
	#   1. 1064 "You have an error in your SQL syntax ..."
	#      Example: TRUNCATE <view>
	#   2. <number <> 1064> <This option/whatever is not applicable to the current object/situation/whatever>
	#   This might look a bit ugly but it has the benefit that these statements are at least tried.
	#   The goal is not to check the parse process, but there might be temporary MDL locks or in worst
	#   case remaining permanent MDL lock. Effects of these locks should be also checked.
	#   Just as a reminder:
	#   A CREATE VIEW which fails with an error <> "You have an error in your SQL syntax" causes an implicit COMMIT
	#   of the current transaction.
	{ $base_piece="" ; $temp_piece="" ; $merge_piece="" ; $part_piece="" ; $view_piece="" ; return undef } ;
separate_normal_sequence:
	# Advantages/Disadvantages: To be discovered
	{ $sequence_piece="_S" ; $normal_piece="_N" ; return undef } ;
no_separate_normal_sequence:
	# Advantages/Disadvantages: To be discovered
	{ $sequence_piece="" ; $normal_piece="" ; return undef } ;
separate_objects:
	# Effect: Distinction between schemas, tables, functions, triggers, procedures and events
	#         Only statements which are applicable to this type of object will be generated.
	#         Example: CALL <existing partitioned table> ... should be not generated.
	# Advantage: Less failing statements, logs are much easier to read
	# Disadvantage: The avoided suitations are not tested.
	{ $database_prefix="testdb" ; $table_prefix="t1_" ; $procedure_prefix="p1_" ; $function_prefix="f1_" ; $trigger_prefix="tr1_" ; $event_prefix="e1_" ; return undef } ;
no_separate_objects:
	# Effect: At least no distinction between functions, triggers, procedures and events
	#         If no_separate_table_types is added, than also tables are no more separated.
	#         Example: CALL <existing partitioned table> ... should be not generated.
	# Advantage: More coverage
	# Disadvantage: More failing statements
	{ $database_prefix="o1_1" ; $table_prefix="o1_" ; $procedure_prefix="o1_" ; $function_prefix="o1_" ; $trigger_prefix="o1_"  ; $event_prefix="o1_" ; return undef } ;

avoid_bugs:
	# Set this grammar item to "empty" if for example no optimizer related server system variable has to be switched.
	;

event_scheduler_on:
	SET GLOBAL EVENT_SCHEDULER = ON ;

event_scheduler_off:
	SET GLOBAL EVENT_SCHEDULER = OFF ;

have_some_initial_objects:
	# It is assumed that this reduces the likelihood of "Table does not exist" significant when running with a small number of "worker" threads.
	# The amount of create_..._table items within the some_..._tables should depend a bit on the value in $namespace_width but I currently
	# do not know how to express this in the grammar.
	# Use if
	#   Bug#47633 assert in ha_myisammrg::info during OPTIMIZE
	# is fixed (merge tables disabled)
	# some_databases ; some_base_tables ; some_temp_tables ; some_merge_tables ; some_part_tables ; some_view_tables ; some_functions ; some_procedures ; some_trigger ; some_events ;
	some_databases ; some_base_tables ; some_temp_tables ; some_part_tables ; some_view_tables ; some_functions ; some_procedures ; some_trigger ; some_events ;
some_databases:
	create_database    ; create_database    ; create_database    ; create_database    ;
some_base_tables:
	create_base_table  ; create_base_table  ; create_base_table  ; create_base_table  ;
some_temp_tables:
	create_temp_table  ; create_temp_table  ; create_temp_table  ; create_temp_table  ;
some_merge_tables:
	create_merge_table ; create_merge_table ; create_merge_table ; create_merge_table ;
some_part_tables:
	create_part_table  ; create_part_table  ; create_part_table  ; create_part_table  ;
some_view_tables:
	create_view        ; create_view        ; create_view        ; create_view        ;
some_functions:
	create_function    ; create_function    ; create_function    ; create_function    ;
some_procedures:
	create_procedure   ; create_procedure   ; create_procedure   ; create_procedure   ;
some_trigger:
	create_trigger     ; create_trigger     ; create_trigger     ; create_trigger     ;
some_events:
	create_event       ; create_event       ; create_event       ; create_event       ;

nothing_disabled:
	{ $sequence_begin = "/* Sequence start */" ; $sequence_end = "/* Sequence end */" ; return undef } ;

system_table_stuff:
	# This is used in "grant_revoke".
	CREATE USER otto@localhost ;


# Useful grammar items ====================================================================================#

rand_val:
	{ $rand_val = $prng->int(0,100) / 100 } ;


# Namespaces of objects ==========================================================================#
# An explanation of the namespace concept is on top of this file.
#
# 1. The database namespace ##########################################################################
database_name_s:
	{ $database_name_s = $database_prefix . $sequence_piece ; $database_name = $database_name_s } ;
database_name_n:
	{ $database_name_n = $database_prefix . $normal_piece   ; $database_name = $database_name_n } ;
database_name:
	# Get a random name from the "database" namespace.
	# $database_name gets automatically filled when database_name_s or database_name_n is executed.
	database_name_s | database_name_n ;


# 2. The base table namespace ########################################################################
base_table_name_s:
	# Get a random name from the "base table long life" namespace.
	{ $base_table_name_s = $table_prefix . $base_piece   . $prng->int(1,$namespace_width) . $sequence_piece ; $base_table_name = $base_table_name_s ; $table_name = $base_table_name } ;
base_table_name_n:
	# Get a random name from the "base table short life" namespace.
	{ $base_table_name_n = $table_prefix . $base_piece   . $prng->int(1,$namespace_width) . $normal_piece   ; $base_table_name = $base_table_name_n ; $table_name = $base_table_name } ;
base_table_name:
	# Get a random name from the "base table" namespace.
	base_table_name_s | base_table_name_n ;

# Sometimes useful stuff:
base_table_item_s:
	database_name_s . base_table_name_s { $base_table_item_s = $database_name_s . " . " . $base_table_name_s ; $base_table_item = $base_table_item_s ; return undef } ;
base_table_item_n:
	database_name   . base_table_name_n { $base_table_item_n = $database_name   . " . " . $base_table_name_n ; $base_table_item = $base_table_item_n ; return undef } ;
base_table_item:
	base_table_item_s | base_table_item_n ;
base_table_item_list_s:
	base_table_item_s | base_table_item_s , base_table_item_s ;
base_table_item_list_n:
	base_table_item_n | base_table_item_n , base_table_item_n ;
base_table_item_list:
	base_table_item   | base_table_item   , base_table_item   ;


# 3. The temp table namespace ########################################################################
# Please note that TEMPORARY merge tables will be not generated.
temp_table_name_s:
	# Get a random name from the "temp table long life" namespace.
	{ $temp_table_name_s = $table_prefix . $temp_piece   . $prng->int(1,$namespace_width) . $sequence_piece ; $temp_table_name = $temp_table_name_s ; $table_name = $temp_table_name } ;
temp_table_name_n:
	# Get a random name from the "temp table short life" namespace.
	{ $temp_table_name_n = $table_prefix . $temp_piece   . $prng->int(1,$namespace_width) . $normal_piece   ; $temp_table_name = $temp_table_name_n ; $table_name = $temp_table_name } ;
temp_table_name:
	# Get a random name from the "temp table" namespace.
	temp_table_name_s | temp_table_name_n ;

# Sometimes useful stuff:
temp_table_item_s:
	database_name_s . temp_table_name_s { $temp_table_item_s = $database_name_s . " . " . $temp_table_name_s ; $temp_table_item = $temp_table_item_s ; return undef } ;
temp_table_item_n:
	database_name   . temp_table_name_n { $temp_table_item_n = $database_name   . " . " . $temp_table_name_n ; $temp_table_item = $temp_table_item_n ; return undef } ;
temp_table_item:
	temp_table_item_s | temp_table_item_n ;
temp_table_item_list_s:
	temp_table_item_s | temp_table_item_s , temp_table_item_s ;
temp_table_item_list_n:
	temp_table_item_n | temp_table_item_n , temp_table_item_n ;
temp_table_item_list:
	temp_table_item   | temp_table_item   , temp_table_item   ;


# 4. The merge table namespace #######################################################################
# Please note that TEMPORARY merge tables will be not generated.
merge_table_name_s:
	# Get a random name from the "merge table long life" namespace.
	{ $merge_table_name_s = $table_prefix . $merge_piece . $prng->int(1,$namespace_width) . $sequence_piece ; $merge_table_name = $merge_table_name_s ; $table_name = $merge_table_name } ;
merge_table_name_n:
	# Get a random name from the "merge table short life" namespace.
	{ $merge_table_name_n = $table_prefix . $merge_piece . $prng->int(1,$namespace_width) . $normal_piece   ; $merge_table_name = $merge_table_name_n ; $table_name = $merge_table_name } ;
merge_table_name:
	# Get a random name from the "merge table" namespace.
	merge_table_name_s | merge_table_name_n ;

# Sometimes useful stuff:
merge_table_item_s:
	database_name_s . merge_table_name_s { $merge_table_item_s = $database_name_s . " . " . $merge_table_name_s ; $merge_table_item = $merge_table_item_s ; return undef } ;
merge_table_item_n:
	database_name   . merge_table_name_n { $merge_table_item_n = $database_name   . " . " . $merge_table_name_n ; $merge_table_item = $merge_table_item_n ; return undef } ;
merge_table_item:
	merge_table_item_s | merge_table_item_n ;
merge_table_item_list_s:
	merge_table_item_s | merge_table_item_s , merge_table_item_s ;
merge_table_item_list_n:
	merge_table_item_n | merge_table_item_n , merge_table_item_n ;
merge_table_item_list:
	merge_table_item   | merge_table_item   , merge_table_item   ;


# 5. The view table namespace ########################################################################
view_table_name_s:
	# Get a random name from the "view table long life" namespace.
	{ $view_table_name_s = $table_prefix . $view_piece   . $prng->int(1,$namespace_width) . $sequence_piece ; $view_table_name = $view_table_name_s ; $table_name = $view_table_name } ;
view_table_name_n:
	# Get a random name from the "view table short life" namespace.
	{ $view_table_name_n = $table_prefix . $view_piece   . $prng->int(1,$namespace_width) . $normal_piece   ; $view_table_name = $view_table_name_n ; $table_name = $view_table_name } ;
view_table_name:
	# Get a random name from the "view table" namespace.
	view_table_name_s | view_table_name_n ;

# Sometimes useful stuff:
view_table_item_s:
	database_name_s . view_table_name_s { $view_table_item_s = $database_name_s . " . " . $view_table_name_s ; $view_table_item = $view_table_item_s ; return undef };
view_table_item_n:
	database_name   . view_table_name_n { $view_table_item_n = $database_name   . " . " . $view_table_name_n ; $view_table_item = $view_table_item_n ; return undef };
view_table_item:
	view_table_item_s | view_table_item_n ;
view_table_item_list_s:
	view_table_item_s | view_table_item_s , view_table_item_s ;
view_table_item_list_n:
	view_table_item_n | view_table_item_n , view_table_item_n ;
view_table_item_list:
	view_table_item   | view_table_item   , view_table_item   ;


# 6. The partitioned table namespace #################################################################
part_table_name_s:
	# Get a random name from the "part table long life" namespace.
	{ $part_table_name_s = $table_prefix . $part_piece   . $prng->int(1,$namespace_width) . $sequence_piece ; $part_table_name = $part_table_name_s ; $table_name = $part_table_name } ;
part_table_name_n:
	# Get a random name from the "part table short life" namespace.
	{ $part_table_name_n = $table_prefix . $part_piece   . $prng->int(1,$namespace_width) . $normal_piece   ; $part_table_name = $part_table_name_n ; $table_name = $part_table_name } ;
part_table_name:
	# Get a random name from the "part table" namespace.
	part_table_name_s | part_table_name_n ;

# Sometimes useful stuff:
part_table_item_s:
	database_name_s . part_table_name_s { $part_table_item_s = $database_name_s . " . " . $part_table_name_s ; $part_table_item = $part_table_item_s ; return undef };
part_table_item_n:
	database_name   . part_table_name_n { $part_table_item_n = $database_name   . " . " . $part_table_name_n ; $part_table_item = $part_table_item_n ; return undef };
part_table_item:
	part_table_item_s | part_table_item_n ;
part_table_item_list_s:
	part_table_item_s | part_table_item_s , part_table_item_s ;
part_table_item_list_n:
	part_table_item_n | part_table_item_n , part_table_item_n ;
part_table_item_list:
	part_table_item   | part_table_item   , part_table_item   ;


# 7. Mixed namespaces of tables ################################################################

# 7.1 All tables ( base/temp/merge tables + views + ... #########################################
table_item_s:
	base_table_item_s | temp_table_item_s | merge_table_item_s | view_table_item_s | part_table_item_s ;
table_item_n:
	base_table_item_n | temp_table_item_n | merge_table_item_n | view_table_item_n | part_table_item_n ;
table_item:
	table_item_s | table_item_n ;

table_list:
	# Less likelihood for lists, because they
	# - are most probably less often used
	# - cause a higher likelihood of "table does not exist" errors.
	table_item | table_item | table_item | table_item | table_item | table_item | table_item | table_item | table_item |
	table_item , table_item ;
	

# 7.2 All tables but no views #######################################################################
table_no_view_item_s:
	base_table_item_s | temp_table_item_s | merge_table_item_s | part_table_item_s ;
table_no_view_item_n:
	base_table_item_n | temp_table_item_n | merge_table_item_n | part_table_item_n ;
table_no_view_item:
	table_no_view_item_s | table_no_view_item_n ;


# 7.3 All base and temp tables + views ##############################################################
#     These grammar elements is used to avoid some partioning related bugs.
base_temp_view_table_item_s:
	base_table_item_s | temp_table_item_s | view_table_item_s | part_table_item_s ;
base_temp_view_table_item_n:
	base_table_item_n | temp_table_item_n | view_table_item_n | part_table_item_n ;
base_temp_view_table_item:
	base_temp_view_table_item_s | base_temp_view_table_item ;


# 8. Other namespaces ##############################################################a
template_table_item:
	# The disabled names are for future use. They cannot work with the current properties of .zz grammars.
	# The problem is that we get in some scenarios tables with differing numnber of columns.
	# { $template_table_item = "test.table0" }              |
	# { $template_table_item = "test.table1" }              |
	# { $template_table_item = "test.table10" }             |
	# { $template_table_item = "test.table0_int" }          |
	{ $template_table_item = "test.table0_int" }          |
	{ $template_table_item = "test.table1_int" }          |
	{ $template_table_item = "test.table10_int" }         |
	{ $template_table_item = "test.table0_int_autoinc" }  |
	{ $template_table_item = "test.table1_int_autoinc" }  |
	{ $template_table_item = "test.table10_int_autoinc" } ;


procedure_name_s:
	# Get a random name from the "procedure long life" namespace.
	{ $procedure_name_s = $procedure_prefix . $prng->int(1,$namespace_width) . $sequence_piece ; $procedure_name = $procedure_name_s } ;
procedure_name_n:
	# Get a random name from the "procedure short life" namespace.
	{ $procedure_name_n = $procedure_prefix . $prng->int(1,$namespace_width) . $normal_piece   ; $procedure_name = $procedure_name_n } ;
procedure_name:
	# Get a random name from the "procedure" namespace.
	procedure_name_s | procedure_name_n ;

# Sometimes useful stuff:
procedure_item_s:
	database_name_s . procedure_name_s { $procedure_item_s = $database_name_s . " . " . $procedure_name_s ; $procedure_item = $procedure_item_s ; return undef } ;
procedure_item_n:
	database_name   . procedure_name_n { $procedure_item_n = $database_name   . " . " . $procedure_name_n ; $procedure_item = $procedure_item_n ; return undef } ;
procedure_item:
	procedure_item_s | procedure_item_n ;

function_name_s:
	# Get a random name from the "function long life" namespace.
	{ $function_name_s  = $function_prefix . $prng->int(1,$namespace_width) . $sequence_piece  ; $function_name = $function_name_s } ;
function_name_n:
	# Get a random name from the "function short life" namespace.
	{ $function_name_n  = $function_prefix . $prng->int(1,$namespace_width) . $normal_piece    ; $function_name = $function_name_n } ;
function_name:
	# Get a random name from the "function" namespace.
	function_name_s | function_name_n ;

function_item_s:
	database_name_s . function_name_s { $function_item_s = $database_name_s . " . " . $function_name_s ; $function_item = $function_item_s ; return undef } ;
function_item_n:
	database_name   . function_name_n { $function_item_n = $database_name   . " . " . $function_name_n ; $function_item = $function_item_n ; return undef } ;
function_item:
	function_item_s | function_item_n ;

trigger_name_s:
	# Get a random name from the "trigger long life" namespace.
	{ $trigger_name_s   = $trigger_prefix . $prng->int(1,$namespace_width) . $sequence_piece ; $trigger_name = $trigger_name_s } ;
trigger_name_n:
	# Get a random name from the "trigger short life" namespace.
	{ $trigger_name_n   = $trigger_prefix . $prng->int(1,$namespace_width) . $normal_piece   ; $trigger_name = $trigger_name_n } ;
trigger_name:
	# Get a random name from the "trigger" namespace.
	trigger_name_s | trigger_name_n ;

trigger_item_s:
	database_name_s . trigger_name_s { $trigger_item_s = $database_name_s . " . " . $trigger_name_s ; $trigger_item = $trigger_item_s ; return undef } ;
trigger_item_n:
	database_name   . trigger_name_n { $trigger_item_n = $database_name   . " . " . $trigger_name_n ; $trigger_item = $trigger_item_n ; return undef } ;
trigger_item:
	trigger_item_s | trigger_item_n ;

event_name_s:
	# Get a random name from the "event long life" namespace.
	{ $event_name_s   = $event_prefix . $prng->int(1,$namespace_width) . $sequence_piece ; $event_name = $event_name_s } ;
event_name_n:
	# Get a random name from the "event short life" namespace.
	{ $event_name_n   = $event_prefix . $prng->int(1,$namespace_width) . $normal_piece   ; $event_name = $event_name_n } ;
event_name:
	# Get a random name from the "event" namespace.
	event_name_s | event_name_n ;

event_item_s:
	database_name_s . event_name_s { $event_item_s = $database_name_s . " . " . $event_name_s ; $event_item = $event_item_s ; return undef } ;
event_item_n:
	database_name   . event_name_n { $event_item_n = $database_name   . " . " . $event_name_n ; $event_item = $event_item_n ; return undef } ;
event_item:
	event_item_s | event_item_n ;

# Here starts the core of the test grammar ========================================================#

query:
	# handler disabled because of
	#    Bug#54401 assert in Diagnostics_area::set_eof_status , HANDLER
	# dml | dml | dml | dml | ddl | transaction | lock_unlock | lock_unlock | flush | handler ;
	dml | dml | dml | dml | ddl | transaction | lock_unlock | lock_unlock | flush ;

########## TRANSACTIONS ####################

transaction:
	start_transaction | commit | rollback |
	start_transaction | commit | rollback |
	start_transaction | commit | rollback |
	SAVEPOINT savepoint_id | RELEASE SAVEPOINT savepoint_id | ROLLBACK work_or_empty TO savepoint_or_empty savepoint_id |
	BEGIN work_or_empty | set_autocommit | kill_query_or_session ;
	# No impact on mdl.cc , lock.cc ..... set_isolation_level ;

savepoint_id:
	A | B ;

set_isolation_level:
	SET SESSION TX_ISOLATION = TRIM(' isolation_level ');

isolation_level:
	REPEATABLE-READ | READ-COMMITTED | SERIALIZABLE ;


start_transaction:
	START TRANSACTION with_consistent_snapshot ;
with_consistent_snapshot:
	| | | | | | | | | WITH CONSISTENT SNAPSHOT ;

# COMMIT/ROLLBACK
#----------------
# 1. RELEASE should be rare
# 2. AND CHAIN RELEASE is nonsense and will get an error
# 3. COMMIT [ WORK ] [ AND [ NO ] CHAIN ] [RELEASE]
#    AND NO CHAIN is the default, no RELEASE is the default
# 4. ROLLBACK [ WORK ] [ AND [ NO ] CHAIN ] [RELEASE]
#    [ TO SAVEPOINT <savepoint specifier> ]
#    You may specify only one of:
#    "[AND [NO] CHAIN]" or "RELEASE" or "TO SAVEPOINT ...".
#    AND NO CHAIN is the default, no RELEASE is the default

commit:
	COMMIT work_or_empty no_chain release_or_empty |
	COMMIT work_or_empty AND CHAIN                 ;
no_chain:
	| | | |
	AND NO CHAIN ;
release_or_empty:
	| | | | | | | | | RELEASE ;

rollback:
	ROLLBACK work_or_empty no_chain release_or_empty |
	ROLLBACK work_or_empty AND CHAIN                 ;

set_autocommit:
	SET AUTOCOMMIT = zero_or_one ;

kill_query_or_session:
#---------------------
# I expect in case
# - a query gets killed that most locks hold by this query are automatically removed.
#   Metadata locks might survive till transaction end.
# - a session gets killed that any lock hold by this session gets automatically removed.
# We will not check the removal here via SQL or sophisticated PERL code.
# We just hope that forgotten locks lead sooner or later to nice deadlocks.
# 
# Attention: 
#    There is some unfortunate sideeffect of KILL <session> .
#    It reduces the probability to detect deadlocks because it
#    might hit a participating session.
#
# Killing a query or session must NOT affect an
# - auxiliary RQG session (= non executor) because this can fool RQG's judgement
#   about the test outcome
# - executor session which is within the early phase when it pulls meta data (table names, column names,
#   data types, ...). This could end up with RQG exit status 255.
#   -> "Can't use an undefined value as an ARRAY reference at lib/GenTest/Generator/FromGrammar.pm line 269."
# This is ensured by the following:
# Every executor pulls meta data and runs after that "query_init".
# Within "query_init" this executor writes his own CONNECTION_ID() into the table test.executors.
# When running KILL for another session only the id's found in test.executors must be selected.
# In case it is planned to kill
# - another session             AFTER
# - own session (suicide)       BEFORE
# the KILL statement the entry within test.executors has to be removed.
#
# Depending on scenario (a session might run COMMIT/ROLLBACK RELEASE) and whatever other effects it might happen that
# 1. A session disappears but the entry is not removed.
#    This is harmless because somewhere later another session will pick the id from test.executors
#    try the kill session and remove the entry.
# 2. A session entry in test.executors does not exist but the session is alife.
#    This is harmless because somewhere later this session will try to remove its' no more existing
#    entry from test.executors and kill himself.
#
# Scenarios covered:
# 1. S1 kills S2
# 2. S1 kills S1
# 3. S1 try to kill S3 which no more exists.
# 4. Various combinations of sessions running 1., 2. or 3.
#
# The various COMMITs should ensure that locking effects caused by activity on test.executors are minimal.
	COMMIT ; own_id     ; delete_executor_entry ; COMMIT ; KILL       @kill_id                                  |
	COMMIT ; own_id     ;                       ; COMMIT ; KILL QUERY @kill_id                                  |
	COMMIT ; minimal_id ;                       ; COMMIT ; KILL       @kill_id ; delete_executor_entry ; COMMIT |
	COMMIT ; minimal_id ;                       ; COMMIT ; KILL QUERY @kill_id                                  ;
own_id:
	SET @kill_id = CONNECTION_ID() ;
minimal_id:
	SELECT MIN(id) INTO @kill_id FROM test . executors ;
delete_executor_entry:
	DELETE FROM test . executors WHERE id = @kill_id ;

ddl:
	database_ddl                                        |
	base_table_ddl  | base_table_ddl  | base_table_ddl  |
	temp_table_ddl  | temp_table_ddl  | temp_table_ddl  |
	# Use if
	#   Bug#47633 assert in ha_myisammrg::info during OPTIMIZE
	# is fixed (merge tables disabled)
	# merge_table_ddl | merge_table_ddl | merge_table_ddl |
	part_table_ddl  | part_table_ddl  | part_table_ddl  |
	view_ddl        | view_ddl        | view_ddl        |
	procedure_ddl   | procedure_ddl   | procedure_ddl   |
	function_ddl    | function_ddl    | function_ddl    |
	trigger_ddl     | trigger_ddl     | trigger_ddl     |
	event_ddl                  |
	truncate_table             |
	drop_table_list            |
	rename_table               |
	# Disabled because of
	#   Bug#54486 assert in my_seek, concurrent DROP/CREATE SCHEMA, CREATE TABLE, REPAIR
	# table_maintenance_ddl      |
	dump_load_data_sequence    |
	grant_revoke               |
	rename_column              |
	sql_mode                   ;
	# "dump_load_data_sequence" with SELECT ... INTO OUTFILE ...; LOAD DATA ... INFILE
	# consists more of DML statements, but we place this here under "ddl" because the
	# statements in "dml" are often executed as prepared statements. And the text after
	# PREPARE st1 FOR must not contain multiple statements.


########## HANDLER ####################
	# The alias within HANDLER ... OPEN is optional. Unfortunately the HANDLER ... READ/CLOSE ... statements
	# do not accept SCHEMA names. Therefore the tablename must be either a table within the current SCHEMA
	# or an alias. We go with alias all time.

handler:
	handler_open  |
	handler_read  |
	handler_close ;

handler_open:
	HANDLER table_no_view_item OPEN as handler_a ;

handler_read:
	HANDLER handler_a READ handler_index comparison_operator ( _digit ) handler_read_part |
	HANDLER handler_a READ handler_index first_next_prev_last           handler_read_part |
	HANDLER handler_a READ               first_next                     handler_read_part ;
handler_index:
	`PRIMARY`     |
	`col_int_key` ;
handler_read_part:
	| where ;
first_next:
	FIRST |
	NEXT  ;
first_next_prev_last:
	FIRST |
	NEXT  |
	PREV  |
	LAST  ;

handler_close:
	HANDLER handler_a CLOSE ;


########## SHOW ####################
# We run here only object related SHOW commands except SHOW STATUS which checks counters
# of OPEN tables etc.
show:
	database_show              |
	table_show                 |
	routine_show               |
	SHOW STATUS                ;

database_show:
	show_databases | show_create_database ;

show_databases:
	SHOW databases_schemas ;
databases_schemas:
	DATABASES | SCHEMAS ;

show_create_database:
	SHOW CREATE database_schema database_name ;

#----------------------------------

table_show:
	show_tables       | show_tables       |
	show_table_status | show_table_status |
	show_create_table | show_create_view  |
	show_open_tables  | show_columns      ;

show_tables:
	SHOW TABLES ;

show_create_table:
	# Works also for views
	SHOW CREATE TABLE table_item ;

show_open_tables:
	SHOW OPEN TABLES IN database_name ;

show_table_status:
	# Works also for views
	SHOW TABLE STATUS ;

show_columns:
	SHOW full COLUMNS from_in table_item show_columns_part ;
full:
	# Only 20 %
	| | | | FULL ;
from_in:
	FROM | IN ;
show_columns_part:
	# Attention: LIKE '_field' does not work, because RQG does not expand _field.
	#            LIKE '%int%' does not work, because RQG expands it to something like LIKE '%822214656%'.
	# FIXME: Add "WHERE"
	| LIKE '%INT%' ;

show_create_view:
	SHOW CREATE VIEW view_table_item ;

#----------------------------------
routine_show:
	show_create_function  | show_function_code  | show_function_status  |
	show_create_procedure | show_procedure_code | show_procedure_status |
	show_triggers         | show_create_trigger |
	show_events           | show_create_event   ;

show_create_function:
	SHOW CREATE FUNCTION function_item ;

show_function_code:
	SHOW FUNCTION CODE function_item ;

show_function_status:
	SHOW FUNCTION STATUS ;

show_create_procedure:
	SHOW CREATE PROCEDURE procedure_item ;

show_procedure_code:
	SHOW PROCEDURE CODE procedure_item ;

show_procedure_status:
	SHOW PROCEDURE STATUS ;

show_triggers:
	SHOW TRIGGERS ;

show_create_trigger:
	SHOW CREATE TRIGGER trigger_item ;

show_events:
	SHOW EVENTS from_in database_name ;

show_create_event:
	SHOW CREATE EVENT event_item_s ;
	
########## SELECTS ON THE INFORMATION_SCHEMA ####################
# We run here only object related SELECTs.
is_selects:
	is_schemata | is_tables | is_columns ;
is_schemata:
	/* database_name */ SELECT * FROM information_schema . schemata WHERE schema_name = TRIM(' $database_name ') ;
is_tables:
	/* table_item */ SELECT * FROM information_schema . tables WHERE table_schema = TRIM(' $database_name ') AND table_name = TRIM(' $table_name ') ;
is_columns:
	/* table_item */ SELECT * FROM information_schema . columns WHERE table_schema = TRIM(' $database_name ') AND table_name = TRIM(' $table_name ') AND column_name = random_field_quoted ;
# 19.1. The INFORMATION_SCHEMA SCHEMATA Table
# 19.2. The INFORMATION_SCHEMA TABLES Table
# 19.3. The INFORMATION_SCHEMA COLUMNS Table
# 19.4. The INFORMATION_SCHEMA STATISTICS Table
# 19.5. The INFORMATION_SCHEMA USER_PRIVILEGES Table
# 19.6. The INFORMATION_SCHEMA SCHEMA_PRIVILEGES Table
# 19.7. The INFORMATION_SCHEMA TABLE_PRIVILEGES Table
# 19.8. The INFORMATION_SCHEMA COLUMN_PRIVILEGES Table
# 19.9. The INFORMATION_SCHEMA CHARACTER_SETS Table
# 19.10. The INFORMATION_SCHEMA COLLATIONS Table
# 19.11. The INFORMATION_SCHEMA COLLATION_CHARACTER_SET_APPLICABILITY Table
# 19.12. The INFORMATION_SCHEMA TABLE_CONSTRAINTS Table
# 19.13. The INFORMATION_SCHEMA KEY_COLUMN_USAGE Table
# 19.14. The INFORMATION_SCHEMA ROUTINES Table
# 19.15. The INFORMATION_SCHEMA VIEWS Table
# 19.16. The INFORMATION_SCHEMA TRIGGERS Table
# 19.17. The INFORMATION_SCHEMA PLUGINS Table
# 19.18. The INFORMATION_SCHEMA ENGINES Table
# 19.19. The INFORMATION_SCHEMA PARTITIONS Table
# 19.20. The INFORMATION_SCHEMA EVENTS Table
# 19.21. The INFORMATION_SCHEMA FILES Table
# 19.22. The INFORMATION_SCHEMA TABLESPACES Table
# 19.23. The INFORMATION_SCHEMA PROCESSLIST Table
# 19.24. The INFORMATION_SCHEMA REFERENTIAL_CONSTRAINTS Table
# 19.25. The INFORMATION_SCHEMA GLOBAL_STATUS and SESSION_STATUS Tables
# 19.26. The INFORMATION_SCHEMA GLOBAL_VARIABLES and SESSION_VARIABLES Tables
# 19.27. The INFORMATION_SCHEMA PARAMETERS Table
# 19.28. The INFORMATION_SCHEMA PROFILING Table
# 19.29. Other INFORMATION_SCHEMA Tables
#


########## DATABASE ####################
database_ddl:
	create_database   | create_database | create_database |
	drop_database     | alter_database  |
	database_sequence ;

create_database:
	CREATE database_schema if_not_exists database_name_n database_spec ;

database_schema:
	DATABASE | SCHEMA ;

database_spec:
	# We do not want to test CHARACTER SETs and COLLATIONs, but we need something for ALTER DATABASE.
	default_word CHARACTER SET equal utf8 | default_word COLLATE equal utf8_bin ;

drop_database:
	DROP database_schema if_exists database_name_n ;

alter_database:
	ALTER database_schema database_name_n database_spec ;

database_sequence:
	# Have a bigger lifetime for databases because the objects with extended lifetime are stored there.
	$sequence_begin CREATE database_schema database_name_s ; wait_till_drop_database ; DROP database_schema $database_name_s $sequence_end ;
wait_till_drop_database:
	SELECT SLEEP( 2 * rand_val * $life_time_unit ) ;


########## BASE AND TEMPORARY TABLES ####################
base_table_ddl:
	create_base_table   | create_base_table | create_base_table | create_base_table | create_base_table | create_base_table |
	drop_base_table     | alter_base_table  |
	base_table_sequence ;

create_base_table:
	CREATE           TABLE if_not_exists base_table_item_n { $create_table_item = $base_table_item_n ; return undef } create_table_part ;
create_table_part:
	LIKE template_table_item ; ALTER TABLE $create_table_item ENGINE = engine ; INSERT INTO $create_table_item SELECT * FROM $template_table_item |
	LIKE template_table_item ; ALTER TABLE $create_table_item ENGINE = engine ; INSERT INTO $create_table_item SELECT * FROM $template_table_item |
	AS used_select           ;

drop_base_table:
	# DROP two tables is in "drop_table_list"
	DROP           TABLE if_exists base_table_item_n restrict_cascade ;

alter_base_table:
	ALTER ignore TABLE base_table_item_n alter_base_temp_table_part ;

alter_base_temp_table_part:
	# Reasons why "ENGINE = engine" should be rather rare:
	# 1. ALTER ... ENGINE = <engine> is rather rare within a production system running under DML load
	# 2. ALTER ... ENGINE = <engine != MyISAM> "damages" any MERGE table using the affected table as base table.
	#    As a consequence nerly all statements on the MERGE table will fail.
	COMMENT = 'UPDATED NOW()' | COMMENT = 'UPDATED NOW()' | COMMENT = 'UPDATED NOW()' | COMMENT = 'UPDATED NOW()' | COMMENT = 'UPDATED NOW()' |
	COMMENT = 'UPDATED NOW()' | COMMENT = 'UPDATED NOW()' | COMMENT = 'UPDATED NOW()' | COMMENT = 'UPDATED NOW()' |
	ENGINE = engine           ;

base_table_sequence:
	$sequence_begin CREATE TABLE if_not_exists base_table_item_s LIKE template_table_item ; ALTER TABLE $base_table_item_s ENGINE = engine ; INSERT INTO $base_table_item_s SELECT * FROM $template_table_item ; COMMIT ; wait_till_drop_table ; DROP TABLE $base_table_item_s $sequence_end ;

wait_till_drop_table:
	SELECT SLEEP( rand_val * $life_time_unit ) ;

temp_table_ddl:
	# Attention: temp_table_sequence is intentionally omitted, because no other session will be
	#            able to use this table.
	create_temp_table | create_temp_table | create_temp_table | create_temp_table | create_temp_table | create_temp_table |
	drop_temp_table   | alter_temp_table  ;

create_temp_table:
	CREATE TEMPORARY TABLE if_not_exists temp_table_item_n { $create_table_item = $temp_table_item_n ; return undef } create_table_part ;

drop_temp_table:
	# DROP two tables is in "drop_table_list"
	# A pure DROP TABLE is allowed, but we get an implicit COMMITs for that.
	DROP TEMPORARY TABLE if_exists temp_table_item_n |
	DROP           TABLE if_exists temp_table_item_n ;

alter_temp_table:
	ALTER ignore TABLE temp_table_item_n alter_base_temp_table_part ;

########## MAINTENANCE FOR ANY TABLE ####################
# The server accepts these statements for all table types (VIEWs, base tables, ...) though they
# should have partially no effect. We run them on all table types because everything which gets
# accepted has to be checked even if the command should do nothing.
# Example:
#    OPTIMIZE ... TABLE <view> ...
#       Table  Op Msg_type Msg_text
#       test.v1   optimize Error Table 'test.v1' doesn't exist
#       test.v1   optimize status   Operation failed
#    OPTIMIZE ... TABLE <merge table> ...
#       Table  Op      Msg_type        Msg_text
#       test.t1m       optimize        note    The storage engine for the table doesn't support optimize
#
table_maintenance_ddl:
	analyze_table | optimize_table | checksum_table | check_table | repair_table ;

analyze_table:
	ANALYZE not_to_binlog_local TABLE table_list ;
not_to_binlog_local:
	| NO_WRITE_TO_BINLOG | LOCAL ;

optimize_table:
	OPTIMIZE not_to_binlog_local TABLE table_list ;

checksum_table:
	CHECKSUM TABLE table_list quick_extended ;
quick_extended:
	| quick | extended ;
extended:
	# Only 10 %
	| | | | | | | | | EXTENDED ;

check_table:
	CHECK TABLE table_list check_table_options ;
check_table_options:
	| FOR UPGRADE | QUICK | FAST | MEDIUM | EXTENDED | CHANGED ;

repair_table:
	# Use if
	#    Bug#46339 crash on REPAIR TABLE merge table USE_FRM
	# is fixed  (base_temp_table_view_list instead of table_list because of Bug#46339)
	# REPAIR not_to_binlog_local TABLE table_list quick extended use_frm ;
	REPAIR not_to_binlog_local TABLE table_list quick extended ;

use_frm:
	# Only 10 %
	| | | | | | | |  | USE_FRM ;


########## MIXED TABLE RELATED DDL #################################
truncate_table:
	TRUNCATE table_word table_no_view_item_n ;
table_word:
	| TABLE ;

drop_table_list:
	# DROP one table is in "drop_*table"
	# 1. We mix here all tables except VIEWs up.
	# 2. We have an increased likelihood that the statement fails because of use of
	#    - "temporary" (only correct in case of a temporary table)
	#    - two tables (some might not exist)
	DROP temporary TABLE if_exists table_no_view_item_n , table_no_view_item_n restrict_cascade ;

rename_table:
	# RENAME TABLE works also on all types of tables (includes VIEWs)
	RENAME TABLE rename_item_list ;
rename_item_list:
	rename_item | rename_item , rename_item ;
rename_item:
	# Preserve the object type (base,temp,....) and type (Normal) otherwise debugging becomes difficult and
	# the concept with different lifetimes gets broken.
	base_table_item_n  TO base_table_item_n  |
	temp_table_item_n  TO temp_table_item_n  |
	merge_table_item_n TO merge_table_item_n |
	part_table_item_n  TO part_table_item_n  ;

rename_column:
	ALTER TABLE table_no_view_item_s CHANGE COLUMN column_to_change my_column INT |
	ALTER TABLE table_no_view_item_s CHANGE COLUMN my_column column_to_change INT ;

column_to_change:
	`col_int` | `col_int_key` | `pk` ;


########## MERGE TABLE DDL ####################
merge_table_ddl:
	create_merge_table   | create_merge_table | create_merge_table | create_merge_table   | create_merge_table |  create_merge_table |
	drop_merge_table     | alter_merge_table  |
	merge_table_sequence ;

create_merge_table:
	# There is a high risk that the tables which we pick for merging do not fit together because they
	# have different structures. We try to reduce this risk to end up with no merge table at all
	# by the following:
	# 1. Let the merge table have the structure of the first base table.
	#    CREATE TABLE <merge table> LIKE <first base table>
	# 2. Let the merge table be based on the first base table.
	#    ALTER TABLE <merge table> ENGINE = MERGE UNION(<first base table>)
	# 3. Add the second base table to the merge table.
	#    ALTER TABLE <merge table> UNION(<first base table>, <second merge table>)
	merge_init_n build_partner1 ; build_partner2 ; create_merge ;

insert_method:
	| INSERT_METHOD = insert_method_value | INSERT_METHOD = insert_method_value | INSERT_METHOD = insert_method_value ;
insert_method_value:
	NO | FIRST | LAST ;

drop_merge_table:
	# DROP two tables is in "drop_table_list"
	DROP TABLE if_exists merge_table_item_n ;

merge_table_sequence:
	# Notes:
	# There is a significant likelihood that a random picked table names as base for the merge table cannot
	# be used for the creation of a merge table because the corresponding tables
	# - must exist
	# - use the storage engine MyISAM
	# - have the same layout
	# Therefore we create here all we need.
	# The use of "base_table_name_n" for the tables to be merged guarantees that these tables
	# are under full DDL/DML load.
	# I do not DROP the underlying tables at sequence end because I hope that "drop_base_table" or similar will do this sooner or later.
	$sequence_begin merge_init_s build_partner1 ; build_partner2 ; create_merge ; wait_till_drop_table ; DROP TABLE $mt $sequence_end ;

alter_merge_table:
	# We do not change here the UNION because of the high risk that this fails.
	# A simple change of the insert_method_value is also not doable because we
	# would need to mention also the UNION.
	# It is intentional that we use merge_table_name and not merge_table_name_n.
	ALTER ignore TABLE merge_table_item_n COMMENT = 'UPDATED NOW()' ;

merge_init_s:
	/* merge_table_item_s { $mt = $merge_table_item_s ; return undef } consists of ( base_table_item_s { $mp1 = $base_table_item_s ; return undef } , base_table_item_s { $mp2 = $base_table_item_s ; return undef } ) based on template_table_item */ ;
merge_init_n:
	/* merge_table_item_n { $mt = $merge_table_item_n ; return undef } consists of ( base_table_item_n { $mp1 = $base_table_item_n ; return undef } , base_table_item_n { $mp2 = $base_table_item_n ; return undef } ) based on template_table_item */ ;
build_partner1:
	# This also initializes $database_name and $base_table_name which gets used by the other commands within the sequence.
	CREATE TABLE if_not_exists $mp1 LIKE $template_table_item ; ALTER TABLE $mp1 ENGINE = MyISAM ; INSERT INTO $mp1 SELECT * FROM $template_table_item ;
build_partner2:
	# This also initializes $database_name and $base_table_name which gets used by the other commands within the sequence.
	CREATE TABLE if_not_exists $mp2 LIKE $template_table_item ; ALTER TABLE $mp2 ENGINE = MyISAM ; INSERT INTO $mp2 SELECT * FROM $template_table_item ;
create_merge:
	CREATE TABLE if_not_exists $mt LIKE $template_table_item ; ALTER TABLE $mt ENGINE = MERGE UNION ( $mp1 , $mp2 ); COMMIT ;


########## PARTITIONED TABLE DDL ####################
part_table_ddl:
	create_part_table   | create_part_table | create_part_table | create_part_table | create_part_table | create_part_table |
	drop_part_table     |
	alter_part_table    |
	part_table_sequence ;

create_part_table:
	CREATE TABLE if_not_exists part_table_item_n ENGINE = MyISAM partition_algorithm AS SELECT * FROM template_table_item |
	CREATE TABLE if_not_exists part_table_item_n ENGINE = MyISAM partition_algorithm AS SELECT * FROM template_table_item |
	CREATE TABLE if_not_exists part_table_item_n ENGINE = MyISAM partition_algorithm AS used_select                       ;

partition_algorithm:
	# We do not need sophisticated partitioning here.
	PARTITION BY KEY (pk) PARTITIONS 2        |
	PARTITION BY LINEAR HASH(pk) PARTITIONS 3 ;

drop_part_table:
	# DROP two tables is in "drop_table_list"
	DROP TABLE if_exists part_table_item_n ;

alter_part_table:
	ALTER ignore TABLE part_table_item_n alter_part_table_part ;

alter_part_table_part:
	partition_algorithm       |
	COMMENT = 'UPDATED NOW()' ;

part_table_sequence:
	$sequence_begin CREATE TABLE if_not_exists part_table_item_s ENGINE = MyISAM partition_algorithm AS SELECT * FROM template_table_item ; COMMIT ; wait_till_drop_table ; DROP TABLE $part_table_item_s $sequence_end ;


########## VIEW DDL ####################
view_ddl:
	create_view   | create_view | create_view | create_view | create_view | create_view | create_view | create_view |
	drop_view     | alter_view  |
	view_sequence ;
	
create_view:
	CREATE view_replace ALGORITHM = view_algoritm VIEW view_table_item_n AS used_select ;
view_replace:
	# Only 20 %
	| | | | OR REPLACE ;
view_algoritm:
	UNDEFINED | MERGE | TEMPTABLE ;

drop_view:
	DROP VIEW if_exists view_table_item_n restrict_cascade ;

restrict_cascade:
	# RESTRICT and CASCADE, if given, are parsed and ignored.
	| RESTRICT | CASCADE ;

alter_view:
	# Attention: Only changing the algorithm is not allowed.
	ALTER ALGORITHM = view_algoritm VIEW view_table_item_n AS used_select ;

view_sequence:
	$sequence_begin CREATE ALGORITHM = view_algoritm VIEW view_table_item_s AS used_select ; COMMIT ; SELECT wait_short ; DROP VIEW $view_table_item_s $sequence_end ;


########## STORED PROCEDURE DDL ####################
procedure_ddl:
	create_procedure   | create_procedure |
	drop_procedure     | alter_procedure  |
	procedure_sequence ;

create_procedure:
	CREATE PROCEDURE procedure_item_n () BEGIN proc_stmt ; proc_stmt ; END ;
proc_stmt:
	select | update ;

drop_procedure:
	DROP PROCEDURE if_exists procedure_item_n ;

alter_procedure:
	ALTER PROCEDURE procedure_item_n COMMENT 'UPDATED NOW()' ;

procedure_sequence:
	# FIXME: The PROCEDURE should touch base_table_name_s only .
	$sequence_begin CREATE PROCEDURE procedure_item_s () BEGIN proc_stmt ; proc_stmt ; END ; COMMIT ; SELECT wait_short ; DROP PROCEDURE $procedure_item_s $sequence_end ;


########## STORED FUNCTION DDL ####################
function_ddl:
	create_function   | create_function |
	drop_function     | alter_function  |
	function_sequence ;

create_function:
	CREATE FUNCTION function_item_n () RETURNS INTEGER BEGIN func_statement ; func_statement ; RETURN 1 ; END ;
func_statement:
	# All result sets of queries within a function must be processed within the function.
	# -> Use a CURSOR or SELECT ... INTO ....
	SET @my_var = 1 | SELECT MAX( random_field_quoted1 ) FROM table_item INTO @my_var | insert | delete ;

drop_function:
	DROP FUNCTION if_exists function_item_n ;

alter_function:
	ALTER FUNCTION function_item_n COMMENT 'UPDATED NOW()' ;

function_sequence:
	$sequence_begin CREATE FUNCTION function_item_s () RETURNS INTEGER RETURN ( SELECT MOD( COUNT( DISTINCT random_field_quoted1 ) , 10 ) FROM table_item_s ) ; COMMIT ; SELECT wait_short ; DROP FUNCTION $function_item_s $sequence_end ;


########## TRIGGER DDL ####################
trigger_ddl:
	create_trigger   | create_trigger |
	drop_trigger     |
	trigger_sequence ;
	
create_trigger:
	CREATE TRIGGER trigger_item_n trigger_time trigger_event ON base_table_name_n FOR EACH ROW BEGIN trigger_action ; END ;
trigger_time:
	BEFORE | AFTER ;
trigger_event:
	INSERT | DELETE ;
trigger_action:
	insert | replace | delete | update | CALL procedure_item ;

drop_trigger:
	DROP TRIGGER if_exists trigger_item_n ;

trigger_sequence:
	# FIXME: The action within the trigger should touch base_table_name_s only.
	$sequence_begin CREATE TRIGGER trigger_item_s trigger_time trigger_event ON table_item_s FOR EACH ROW BEGIN trigger_action ; END ; COMMIT ; SELECT wait_short ; DROP TRIGGER $trigger_item_s $sequence_end ;


########## EVENT DDL ####################
event_ddl:
	create_event | create_event | create_event | create_event | create_event | create_event | create_event | create_event |
	drop_event   | alter_event  | drop_event   | alter_event  | drop_event   | alter_event  | drop_event   | alter_event  |
	event_scheduler_on | event_scheduler_off ;
create_event:
	CREATE EVENT if_not_exists event_item_s ON SCHEDULE EVERY 10 SECOND STARTS NOW() ENDS NOW() + INTERVAL 21 SECOND completion_handling DO SELECT * FROM table_item LIMIT 1 ;
completion_handling:
	ON COMPLETION not_or_empty PRESERVE ;
drop_event:
	DROP EVENT if_exists event_item_s ;
alter_event:
	# Disabled because of
	#    Bug#44171 KILL ALTER EVENT can crash the server
	# ALTER EVENT event_item_s COMMENT 'UPDATED NOW()';
	;

########## DML ####################

dml:
	# Have only 10 % prepared statements.
	#    SQL Statements to be handled via PREPARE, EXECUTE and DEALLOCATE cause a bigger amount of
	#    failing statements than SQL statements which are executed in non prepared mode.
	#    The reason is that we run the EXECUTE and DEALLOCATE independent of the outcome of the
	#    PREPARE. So if the PREPARE fails because some table is missing, we loose the old
	#    prepared statement handle, if there was any, and get no new one. Therefore the succeeding
	#    EXECUTE and DEALLOCATE will also failcw because of missing statement handle.
	dml2 | dml2 | dml2 | dml2 | dml2 | dml2 | dml2 | dml2 | dml2 |
	PREPARE st1 FROM " dml2 " ; EXECUTE st1 ; DEALLOCATE PREPARE st1 ;

dml2:
	select | select | select  |
	# is_selects disabled because of
	#    Bug #54678  	InnoDB, TRUNCATE, ALTER, I_S SELECT, crash or deadlock
	# do     | insert | replace | delete | update | CALL procedure_item | show | is_selects ;
	do     | insert | replace | delete | update | CALL procedure_item | show ;

########## DO ####################
do:
	DO 1                                                                                                   |
	# A lot options like HIGH_PRIORITY (after SELECT ) etc. are not allowed in connection with DO.
	# The SELECT must give one column.
	DO ( SELECT COUNT(*) FROM table_item WHERE `pk` BETWEEN _digit[invariant] AND _digit[invariant] + 20 ) |
	DO user_lock_action                                                                                    ;

user_lock_action:
	IS_FREE_LOCK(TRIM(' _digit '))                                |
	IS_USED_LOCK(TRIM(' _digit '))                                |
	RELEASE_LOCK(TRIM(' _digit '))                                |
	GET_LOCK(TRIM(' _digit '), 0.5 * rand_val * $life_time_unit ) ;

########## SELECT ####################
select:
	select_normal | select_normal | select_normal | select_normal | select_with_sleep ;

select_normal:
	# select = Just a query = A statement starting with "SELECT".
	select_part1 addition into for_update_lock_in_share_mode ;

select_with_sleep:
	# Run a SELECT which holds locks (if there are any) longer.
	SELECT 1 FROM table_item WHERE wait_short = 0 LIMIT 1 ;

used_select:
	# used_select = The SELECT used in CREATE VIEW/TABLE ... AS SELECT, INSERT INTO ... SELECT
	# "PROCEDURE ANALYSE" and "INTO DUMPFILE/OUTFILE/@var" are not generated because they
	# are partially disallowed or cause garbage (PROCEDURE).
	select_part1 addition_no_procedure ;

select_part1:
	SELECT high_priority cache_results table_field_list_or_star FROM table_in_select as A ;

cache_results:
	| sql_no_cache | sql_cache ;
sql_no_cache:
	# Only 10 %
	| | | | | | | | |
	SQL_NO_CACHE ;
sql_cache:
	# Only 10 %
	| | | | | | | | |
	SQL_CACHE ;

table_in_select:
	# Attention: In case of CREATE VIEW a subquery in the FROM clause (derived table) is disallowed.
	#            Therefore they should be rare.
	table_item | table_item | table_item | table_item | table_item |
	( SELECT table_field_list_or_star FROM table_item ) ;

addition:
	# Involve one (simple where condition) or two tables (subquery | join | union)
	where procedure_analyze | subquery procedure_analyze | join where procedure_analyze | procedure_analyze union where ;

addition_no_procedure:
	# Involve one (simple where condition) or two tables (subquery | join | union)
	# Don't add procedure_analyze.
	where | where | where | where | where | where | where |
	subquery | join where | union where ;

where:
	# The very selective condition is intentional.
	# It should ensure that
	# - result sets (just SELECT) do not become too big because this affects the performance in general and
	#   the memery consumption of RQG (I had a ~ 3.5 GB virt memory RQG perl process during some simplifier run!)
	# - tables (INSERT ... SELECT, REPLACE) do not become too big
	# - tables (DELETE) do not become permanent empty
	# Please note that there are some cases where LIMIT cannot be used.
	WHERE `pk` BETWEEN _digit[invariant] AND _digit[invariant] + 1 | WHERE function_item () = _digit AND `pk` = _digit ;

union:
	UNION SELECT * FROM table_in_select as B ;

join:
	# Do not place a where condition here.
	NATURAL JOIN table_item B ;

subquery:
	correlated | non_correlated ;
subquery_part1:
	WHERE A.`pk` IN ( SELECT `pk` FROM table_item AS B WHERE B.`pk` = ;
correlated:
	subquery_part1 A.`pk` ) ;
non_correlated:
	subquery_part1 _digit ) ;

procedure_analyze:
	# Correct place of PROCEDURE ANALYSE( 10 , 2000 )
	# 0. Attention: The result set of the SELECT gets replaced  by PROCEDURE ANALYSE output.
	# 1. WHERE ... PROCEDURE (no UNION of JOIN)
	# 2. SELECT ... PROCEDURE UNION SELECT ... (never after UNION)
	# 3. SELECT ... FROM ... PROCEDURE ... JOIN (never at statement end)
	# 4. Never in a SELECT which does not use a table
	# 5. Any INTO DUMPFILE/OUTFILE/@var must be after PROCEDURE ANALYSE.
	#    The content of DUMPFILE/OUTFILE/@var is from the PROCEDURE ANALYSE result set.
	# 6. CREATE TABLE ... AS SELECT PROCEDURE -> The table contains the PROCEDURE result set.
	# 7. INSERT ... SELECT ... PROCEDURE -> It's tried to INSERT the PROCEDURE result set.
	#    High likelihood of ER_WRONG_VALUE_COUNT_ON_ROW
	# Only 10 %
	| | | | | | | |  |
	PROCEDURE ANALYSE( 10 , 2000 ) ;

into:
	# Only 10 %
	| | | | | | | | |
	INTO into_object ;

into_object:
	# INSERT ... SELECT ... INTO DUMPFILE/OUTFILE/@var is not allowed
	# This also applies to CREATE TABLE ... AS SELECT ... INTO DUMPFILE/OUTFILE/@var
	# 1. @_letter is in average not enough variables compared to the column list.
	#    -> @_letter disabled till I find a solution.
	# 2. DUMPFILE requires a result set of one row
	#    Therefore 1172 Result consisted of more than one row is very likely.
	# OUTFILE _tmpnam | DUMPFILE _tmpnam | @_letter ;
	OUTFILE _tmpnam ;

for_update_lock_in_share_mode:
	| for_update | lock_share ;
for_update:
	# Only 10 %
	| | | | | | | | |
	FOR UPDATE ;
lock_share:
	# Only 10 %
	| | | | | | | | |
	LOCK IN SHARE MODE ;


########## INSERT ####################
insert:
	insert_normal | insert_normal | insert_normal | insert_normal | insert_with_sleep ;
insert_normal:
	INSERT low_priority_delayed_high_priority ignore into_word table_item simple_or_complicated on_duplicate_key_update ;
simple_or_complicated:
	( random_field_quoted1 ) VALUES ( digit_or_null ) |
	braced_table_field_list used_select LIMIT 1 ;
on_duplicate_key_update:
	# Only 10 %
	| | | | | | | | |
	ON DUPLICATE KEY UPDATE random_field_quoted1 = _digit ;
insert_with_sleep:
	INSERT ignore INTO table_item ( table_field_list ) SELECT $table_field_list FROM table_item WHERE wait_short = 0 LIMIT 1 ;


########## REPLACE ####################
replace:
	# 1. No ON DUPLICATE .... option. In case of DUPLICATE key it runs DELETE old row INSERT new row.
	# 2. HIGH_PRIORITY is not allowed
	REPLACE low_priority_delayed into_word table_item simple_or_complicated ;


########## DUMP_LOAD_DATA ####################
dump_load_data_sequence:
	# We omit a lot stuff which could be assigned after the table name. This stuff should
	# be important for locking tests.
	# We generate an outfile so that we have a chance to find an infile.
	# Go with the next command as soon as "LOCAL" is supported. (not supported in 5.4)
	# generate_outfile ; LOAD DATA low_priority_concurrent local_or_empty INFILE tmpnam replace_ignore INTO TABLE table_item ;
	generate_outfile ; LOAD DATA low_priority_concurrent INFILE tmpnam replace_ignore INTO TABLE table_item ;
generate_outfile:
	SELECT * FROM template_table_item INTO OUTFILE _tmpnam ;
low_priority_concurrent:
	| low_priority | concurrent ;
concurrent:
	# Only 20 % <> empty.
	| | | | CONCURRENT ;
replace_ignore:
	| replace_option | ignore ;


########## GRANT_REVOKE ####################
# We mix here some trouble I can imagine on mysql.tables_priv.  It's basically how we access it's content.
grant_revoke:
	GRANT  ALL ON table_item TO otto@localhost                                           |
	REVOKE ALL ON table_item FROM otto@localhost                                         |
	SELECT COUNT(*) FROM mysql.tables_priv WHERE user = LOWER('OTTO')                    |
	DELETE FROM mysql.tables_priv WHERE user = LOWER('OTTO') ; FLUSH PRIVILEGES          |
	/* table_item */ INSERT INTO mysql.tables_priv (host,db,user,table_name,grantor,table_priv) VALUES (LOWER('LOCALHOST'),TRIM(' $database '),LOWER('OTTO'),TRIM(' $table_name '),LOWER('ROOT@LOCALHOST'),'Select') ; FLUSH PRIVILEGES |
	SELECT COUNT(*) FROM information_schema.table_privileges WHERE grantee LIKE '%OTTO%' |
	SHOW GRANTS FOR otto@localhost ;

########## SQL MODE ########################
sql_mode:
	empty_mode | empty_mode | empty_mode | empty_mode |
	empty_mode | empty_mode | empty_mode | empty_mode |
	empty_mode | empty_mode | empty_mode | empty_mode |
	traditional_mode ;
empty_mode:
	SET SESSION SQL_MODE='' ;
traditional_mode:
	SET SESSION SQL_MODE=LOWER('TRADITIONAL');


########## DELETE ####################
# FIXME: DELETE IGNORE is missing
delete:
	delete_normal | delete_normal | delete_normal | delete_normal | delete_with_sleep ;
delete_normal:
	# LIMIT row_count is disallowed in case we have a multi table delete.
	# Example: DELETE low_priority quick ignore A , B FROM table_item AS A join where LIMIT _digit |
	# DELETE is ugly because a table alias is not allowed.
	DELETE low_priority quick ignore       FROM table_item      WHERE `pk` > _digit LIMIT 1 |
	DELETE low_priority quick ignore A , B FROM table_item AS A join where                  |
	DELETE low_priority quick ignore A     FROM table_item AS A where_subquery              ;
where_subquery:
	where | subquery ;
delete_with_sleep:
	DELETE low_priority quick       FROM table_item      WHERE   `pk` + wait_short = _digit ;


########## UPDATE ####################
update:
	update_normal | update_normal | update_normal | update_normal | update_with_sleep ;
update_normal:
	UPDATE low_priority ignore table_item SET random_field_quoted1 = _digit WHERE `pk` > _digit LIMIT _digit  |
	UPDATE low_priority ignore table_item AS A join SET A. random_field_quoted1 = _digit , B. random_field_quoted1 = _digit ;
update_with_sleep:
	UPDATE low_priority ignore table_item SET random_field_quoted1 = _digit WHERE wait_short = 0 LIMIT 1 ;


########## LOCK/UNLOCK ####################
lock_unlock:
	lock | unlock | unlock | unlock | unlock ;
lock:
	LOCK TABLES lock_list ;
lock_list:
	# Less likelihood for lists, because they
	# - are most probably less often used
	# - cause a higher likelihood of "table does not exist" errors.
	lock_item | lock_item | lock_item | lock_item | lock_item | lock_item | lock_item | lock_item | lock_item |
	lock_item , lock_item ;
lock_item:
	# Have a low risk to get a clash of same table alias.
	table_item AS _letter lock_type ;
lock_type:
	# Disabled because of
	#    Bug#54117 crash in thr_multi_unlock, temporary table
	#    Bug#54553 Innodb asserts in ha_innobase::update_row, temporary table, table lock
	# READ local_or_empty      |
	# low_priority WRITE       |
	IN SHARE MODE nowait     |
	IN SHARE MODE nowait     |
	IN SHARE MODE nowait     |
	IN EXCLUSIVE MODE nowait |
	IN EXCLUSIVE MODE nowait |
	IN EXCLUSIVE MODE nowait ;
nowait:
	NOWAIT | ;

unlock:
	UNLOCK TABLES ;


########## FLUSH ####################
flush:
	# WITH READ LOCK causes that nearly all following statements will fail with
	# Can't execute the query because you have a conflicting read lock
	# Therefore it should
	# - be rare
	# - last only very short time
	# So I put it into a sequence with FLUSH ... ; wait a bit ; UNLOCK TABLES
	FLUSH TABLE table_list | FLUSH TABLE table_list | FLUSH TABLE table_list | FLUSH TABLE table_list | FLUSH TABLE table_list |
	FLUSH TABLE table_list | FLUSH TABLE table_list | FLUSH TABLE table_list | FLUSH TABLE table_list | FLUSH TABLE table_list |
	FLUSH TABLE table_list | FLUSH TABLE table_list | FLUSH TABLE table_list | FLUSH TABLE table_list | FLUSH TABLE table_list |
	FLUSH TABLE table_list | FLUSH TABLE table_list | FLUSH TABLE table_list | FLUSH TABLE table_list ;
	# Disabled because of
	#    Bug#54436 Deadlock on concurrent FLUSH WITH READ LOCK, ALTER TABLE, ANALYZE TABLE
	# (concurrent
	#  - ANALYZE TABLE t1
	#  - ALTER TABLE t1 ENGINE = InnoDB
	#  - FLUSH TABLES WITH READ LOCK ; SELECT SLEEP( 0.01 ) ; UNLOCK TABLES; )
	# FLUSH TABLES WITH READ LOCK ; SELECT wait_short ; UNLOCK TABLES ;


########## TINY GRAMMAR ITEMS USED AT MANY PLACES ###########
as:
	| AS ;

braced_table_field_list:
	# In case of <empty> for braced_table_field_list we have a significant fraction of
	# INSERT/REPLACE INTO <table> <no field list>
	# failing with: 1394 Can not insert into join view 'test.t1_view_0_S' without fields list
	# Therefore <empty> is only 20 %.
	( table_field_list ) | ( table_field_list ) | ( table_field_list ) | ( table_field_list ) | ;

comparison_operator:
	=  |
	<= |
	>= |
	<  |
	>  ;


default_word:
	| DEFAULT ;

digit_or_null:
	_digit | _digit | _digit | _digit | _digit | _digit | _digit | _digit | _digit |
	NULL ;

engine:
	MEMORY | MyISAM | InnoDB ;

equal:
	| = ;

delayed:
	# Only 10 %
	| | | | | | | | | DELAYED ;

high_priority:
	# Only 20 %
	| | | | HIGH_PRIORITY ;

ignore:
	# Only 10 %
	| | | | | | | | |
	IGNORE ;

if_exists:
	# 90 %, this reduces the amount of failing DROPs
	| IF EXISTS | IF EXISTS | IF EXISTS | IF EXISTS | IF EXISTS | IF EXISTS | IF EXISTS | IF EXISTS | IF EXISTS ;

if_not_exists:
	# 90 %, this reduces the amount of failing CREATEs
	| IF NOT EXISTS | IF NOT EXISTS | IF NOT EXISTS | IF NOT EXISTS | IF NOT EXISTS | IF NOT EXISTS | IF NOT EXISTS | IF NOT EXISTS | IF NOT EXISTS ;

into_word:
	# Only 50 %
	| INTO ;

local_or_empty:
	# Only 20%
	| | | | LOCAL ;

low_priority_delayed_high_priority:
	| low_priority | delayed | high_priority ;

low_priority_delayed:
	| low_priority | delayed ;

low_priority:
	# Only 10 %
	| | | | | | | | |
	LOW_PRIORITY ;

no_or_empty:
	| NO ;

not_or_empty:
	| NOT ;

quick:
	# Only 10 %
	| | | | | | | | |
	QUICK ;

random_field_quoted:
	'int_key' | 'int' | 'pk' ;

random_field_quoted1:
	`col_int_key` | `col_int` | `pk` ;

replace_option:
	# Only 20 % <> empty.
	| | | | REPLACE ;

savepoint_or_empty:
	SAVEPOINT | ;

sql_buffer_result:
	# Only 50%
	| SQL_BUFFER_RESULT ;

table_field_list_or_star:
	table_field_list | table_field_list | table_field_list | table_field_list |
	{ $table_field_list = "*" }                                               ;

table_field_list:
	# It is intentional that the next line will lead to ER_FIELD_SPECIFIED_TWICE
	# in case it is used in INSERT INTO <table> ( table_field_list )
	# Disabled because of
	#    Bug#54106 assert in Protocol::end_statement, INSERT IGNORE ... SELECT ... UNION SELECT ...
	# { $table_field_list = "`pk`      , `col_int_key` , `pk`          "} |
	{ $table_field_list = "`col_int_key` , `col_int`     , `pk`      "} |
	{ $table_field_list = "`col_int_key` , `pk`      , `col_int`     "} |
	{ $table_field_list = "`col_int`     , `pk`      , `col_int_key` "} |
	{ $table_field_list = "`col_int`     , `col_int_key` , `pk`      "} |
	{ $table_field_list = "`pk`      , `col_int`     , `col_int_key` "} |
	{ $table_field_list = "`pk`      , `col_int_key` , `col_int`     "} ;

temporary:
	# Attention:
	# Do not apply CREATE/DROP TEMPORARY on "long life" whatever tables.
	# Use "short life" (-> <whatever>_n) tables only.
	# 1. In case of "long life" (-> <whatever>_s) tables the CREATE and DROP must be within
	#    a sequence with some "wait_till_drop_table" between. TEMPORARY tables are session specific.
	#    So no other session can use this table.
	# 2. In case of "short life" tables the CREATE and DROP are isolated. So the session
	#    which created the table will pick a random statement and maybe do something on
	#    the table <> DROP.
	# Only 10 % because no other session can use this table.
	| | | | | | | | |
	TEMPORARY ;

wait_short:
	SLEEP( 0.5 * rand_val * $life_time_unit ) ;

work_or_empty:
	| WORK ;

zero_or_one:
	0 | 1 ;

