# Copyright (c) 2011,2012 Oracle and/or its affiliates. All rights reserved.
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

# Grammar for testing
# - concurrent DML, DDL, FLUSH, LOCK/UNLOCK, transactions
#   The main focus is on anything which has to do with locking
#   - by the server           Example: Meta data locks
#   - by some storage engine  Example: Row locks
# - SQL syntax supported by the server in general
#
# Neither the grammar nor some RQG validator or reporter is able to check
# if server responses, results sets etc. are correct.
# This means that this grammar is for hunting coarse grained bad effects like
#    crashes, deadlocks, database corruptions
# only.
#
# Recommendations:
# - concurrent_1.zz should be used for the generation of some initial data.
#   The purposes of the current test required to use concepts which differ
#   a lot from the standard RQG concept "create whatever tables with the data
#   generation grammar and the sql grammar will pick whatever it finds and
#   work well". This is not valid here. Other non adjusted data generation
#   grammars will reduce the value of the test to a huge extend because
#   they most probably do not follow the concepts of this test.
# - Do not use the RQG validator "ErrorMessageCorruption" in connection with
#   this grammar. You will get false "database corruption" alarms because
#   server messages will contain binary data.
#   Reason: Attempts to insert data from a BLOB column into an integer column
#           might fail and than the server prints this binary data within
#           the error message.
# - Standard RQG setup
#   - validators 'None'.
#   - reporters  'Deadlock,Backtrace,Shutdown,ErrorLog'
#   - threads    some value between 6 and 32, 12 is quite good
#   - queries    30000
#   - duration   1200
#
#
# Created:
#    2009-07 Matthias Leich
#            WL#5004 Comprehensive Locking Stress Test for Azalea
#               A few grammar rules were taken from other grammar files.
#
# Last Modifications:
#    2012-04 Matthias Leich
#            Copy the
#               WL5004_* grammars
#               (adjusted to MySQL 5.5 properties)
#            to
#               concurrency_1.*
#            and perform any adjustments to MySQL 5.6 properties there.
#            - Add create/drop FOREIGN KEY
#              This has to work correct anyway but was added for the future
#              WL#6049 Meta-data locking for FOREIGN KEY tables
#            - A partitioned table could also use InnoDB
#            - Minor cleanup in grammar
#            - Modified ALTER TABLE syntax
#              This was for WL#5534 Online ALTER, Phase 1
#            - Add more variants of alter table especially add/drop primary key
#              and/or index + definition with prefix
#            - Switch to template tables with different structure
#            - concurrent_1.yy
#    2011-05 Jon Olav Hauglid
#               Updated lock_type: to reflect that WL#3561 Transactional
#               LOCK TABLE has been cancelled.
#
# TODO:
#   - temporary enable some disabled component within the rule "rare_ddl", test,
#     report bugs if necessary, enable the component permanent when bug fixed iterations
#   - Around FOREIGN KEYs and blobs:
#     A statement like   ALTER TABLE testdb_S . t1_part3_N ADD CONSTRAINT idx4
#     FOREIGN KEY (  col_int, col_blob ) REFERENCES testdb_N . t1_part2_N (  col_int, col_blob ) ;
#     is most probably not what works well. BLOBs are too long.
#
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
#     Simple(idea of Philip):
#        Using an I_S in a meaningless subselect would be best, just have
#             ( SELECT user + 0 FROM INFORMATION_SCHEMA.USERS LIMIT 1)
#     Complete(idea of mleich)
#        But IS tables used in VIEWs, SELECT, DELETE/UPDATE subqueries/join, PROCEDURES etc.
#        are complete missing. Could I inject this in a subquery?
#   - Simplify grammar:
#           The namespace concept is good for grammar development, avoiding failing statements,
#           understanding statement logs but it seems to have some bad impact on grammar simplification
#           speed. Experiment a bit.
#           Idea:
#           - Have a pool (PERL hash, list or array) for names of tables to be used in generation
#             of some kind of statements. If we need a table name for this kind of statements than
#             we pick it from this table.
#             Advantage: All these statements get table names from some "pick table name from pool" rule
#                        and not the already existing crowd of name generation rules.
#                        So in case simplification for such statement generating rules is done than
#                        we have to deal with far way less rules.
#             Issues: We need most probably several of such pools and the maintenance of the content
#                     of the pool is not for free.
#                     Doubts: Is using the "pick table name from pool" rule as "stop rule" really
#                             an advantage? Do we simply hide stuff and end up with less simplified
#                             grammars?
#           - Whenever we try to create some new table we
#             - calculate the name by using the existing name generation rules
#               Issue: Most probably none of the existing rules can be removed from the grammar.
#             - add the name of this table to the corresponding pools
#               Issue: In case we have many queries, than we have many CREATE TABLE statements too.
#                      This means that we add the same name many times to the pool.
#                      An endless growing list or array would cause trouble with memory consumption.
#                      So some "do not have duplicates" mechanism seems to be required.
#             Advantage:
#             Omit names of at least some not existing tables during statement generation.
#               Example1:
#                  In case the "create merge table" rules are set to empty or not used
#                  than no merge table names will be added to the pool. So other statements
#                  will not use a merge table name (-> no failure because of missing merge table).
#                  This advantage is valid for the complete runtime of the test.
#               Example2:
#                  Around the beginning of some RQG test run with this grammar there are none or just
#                  a few tables which exist and can be used in statements. This means that we have
#                  with the current grammar a rather high likelihood that we generate some name of
#                  a table which does not exist.
#                  Picking some table name from the pool raises the likelihood that the table exists
#                  because we have at least tried to create this table. Please note that the
#                  - create table statement might have failed
#                  - the create table statement might have had success but also some drop table/schema
#                    which removed the object
#                  So the advantage is medium at the beginning and shrinks to nothing after some time.
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
# Hints and experiences (important when extending this grammar):
# -------------------------------------------------------------------
# 1. Any statement sequence has to be in one line.
# 2. Be aware of the dual use of ';'. It separates SQL statements in sequences and closes the definition block
#    of a grammar rules. So any ';' before some '|' has a significant impact.
# 3. Strange not intended effects might be caused by '|' or ':' instead of ';'
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
# 8. A rule definitions is a list of components/alternatives. Put the most simple (refers to failure analysis) option first
#    in such lists. The automatic grammar simplification walks basically from the last component into the direction of the
#    first one. So placing the most comfortable alternative first raises the likelihood that it survives.
#    Example:
#    Before simplification:
#    where:
#       <empty> | WHERE `pk` BETWEEN _digit AND _digit | WHERE function_name_n() = _digit ;
#    Maybe after simplification:
#    where:
#       <empty> ;
#    --> RQG generates simple statements without WHERE condition.
# 9. The RQG "Reporter" LockTableKiller can help to accelerate "deadlocked" tests.
# 10. The grammar should not generate statements which fail because of wrong syntax.
#     Such statements
#     - do not increase the coverage for locking
#     - reduce the stress for the server caused by the current test
#     - are better done via some dedicated MTR based test
# 11. The grammar is allowed to generate statements which fail because it is tried to
#     apply some operation to an object which does not exist or is of wrong type.
#     Example: SELECT ... FROM t1 JOIN t2 ... where t2 does not exist.
#     Such statements
#     - cannot be avoided because with the current architecture of the grammar we
#       are unable to ensure that everything fits.
#       This is more intentional than unwanted.
#     - cause MDL locks which belong to our testing area.
#       Therefore such statements should be generated.
#     - should be not that frequent because this might decrease the stress on the server.
# 12. How to extend the grammar in order to cover some historic bug:
#     Example: Let's assume some fixed bug in parser or optimizer.
#              The replay test case form the bug report contains that some specific
#              SELECT applied to some specific table t1 or some structure crashed the server.
#     Never add rules to the server which cause that exact this table is exposed
#     exact this statement.
#     Bad example:
#     query:
#        bug123456 | ddl | dml ....
#     bug123456:
#        CREATE TABLE t1 ... ; INSERT INTO t1 ... ; SELECT ... FROM t1 ... ; DROP TABLE t1;
#     Such a test should be implemented as MTR based test and never in some RQG grammar.
#     Reasons:
#     1. The ongoing costs for execution, analysis in case of failures and maybe required
#        maintenance (example: Intentional change of supported syntax) for a MTR based
#        test are significant smaller than for a RQG based test.
#     2. The current grammar has already reached some size which makes their use
#        - quite effective in catching bugs but also
#        - very complicated and costly during failure analysis.
#     In case some historic bug fits good into the purpose of this grammar
#     - SQL in general
#     - locking
#     than the grammar should be extended in the following way:
#     1. Add rules to the grammar which cause that more or less excessive variations of the
#        historic bug scenario are generated.
#        - some of the variations must be able to reveal a regression of the historic bug
#        - some of the variations are allowed to be not sensitive to a regression
#     2. The rules added should reuse as much of the existing grammar rules and the objects
#        as possible.
#     3. It cannot be avoided and is definitely acceptable that a RQG run with this grammar
#        and decreased randomness (static seed value + strong limitation of threads or
#        queries or runtime) might be unable to reveal if the historic bug occurs again.
#        The default use case of RQG with this grammar will and must not have such limitations.
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

# End of grammar rule name (default) | characteristic
# -------------------------------------------------------
# _S                                 | related to "sequence" object
# _N                                 | related to "normal" object
#
# Within grammar rule name  | characteristic
# -----------------------------------------------
# _name                     | name of the object
# _item                     | <schema name> . <name of the object>
# _list                     | either single rule (<schema name> . <name of the object>) or comma separated list
#                           | of such rules

#
# Missing but not really important improvements:
# - Reduce the amount of cases where "sequence" objects have "normal" objects within their definition.
#   --> views,functions,procedures
# - Reduce the amount of cases where the wrong table types occur within object definitions
#   Example: TABLE for a TRIGGER or VIEW definition.
#            The current rules allow that names of temporary tables could be generated.
#            But temporary tables are not allowed within TRIGGERs etc.
#


# Section of easy changeable rules with high impact on the test =============================================#
query_init:
	# Variant 1:
	#    Advantage: Less failing (table does not exist ...) statements within the first phase of the test.
	# init_basics1 : init_basics2 ; init_namespaces ; init_executor_table ; event_scheduler_on ; have_some_initial_objects ;
	# Variant 2:
	#    Advantage: Better performance during bug hunt, test simplification etc. because objects are created at
	#               one place (<object>_ddl) only and not also in "have_some_initial_objects".
	init_basics1 ; init_basics2 ; init_namespaces ; init_executor_table ;

init_executor_table:
   # This is an AUXILIARY table which is used here and in kill_query_or_session only.
   # You must not use this table anywhere else.
   # There are no high requirements regarding the capabilities of the storage engine used.
   CREATE TABLE IF NOT EXISTS test . executors (id BIGINT, PRIMARY KEY(id)) ENGINE = InnoDB ; INSERT HIGH_PRIORITY IGNORE INTO test.executors SET id = CONNECTION_ID() ; COMMIT ;

init_basics1:
	# 1. $life_time_unit = maximum lifetime of a table created within a CREATE, wait, DROP sequence.
   #
   #    Attention: This is some "planned" lifetime because we cannot guarantee that
   #               that the CREATE or the DROP ends with success.
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
	#    foreign key     | 1   * RAND() * $life_time_unit
	#
	#    A DML statement using SLEEP will use 0.5 * RAND() * $life_time_unit seconds.
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
	{$life_time_unit = 1; $namespace_width = 3; if ($ENV{RQG_THREADS} == 1) {$life_time_unit = 0} ; return undef} avoid_bugs ; nothing_disabled ; system_table_stuff ;

init_basics2:
	# Store information if we have a debug server in $out_file.
	# 1. The "Home" directory of
	#    - the server would be <vardir>/master-data/
	#      <vardir> is calculated by MTR and affected by options given to runall.pl
	#    - RQG is <RQG install directory>
	# 2. The environment does not contain any variable pointing to <vardir> or RQG directories
	# Therefore we need a $out_file with absolute path.
	{$out_file='/tmp/'.$$.'.tmp'; unlink($out_file); return undef} SELECT VERSION() LIKE '%debug%' INTO OUTFILE {return "'".$out_file."'"};

init_namespaces:
	# Please choose between the following alternatives
	#    separate_objects         -- no_separate_objects
	#    separate_normal_sequence -- no_separate_normal_sequence
	#    separate_table_types     -- no_separate_table_types
	# 1. Lower amount of failing statements
	separate_objects ; separate_normal_sequence ; separate_table_types ;
	# 2. Higher amount of failing statements
	# separate_objects ; separate_normal_sequence ; no_separate_table_types ;
	# 3. Total chaos and high amount of failing statements
	# no_separate_objects ; separate_normal_sequence ; no_separate_table_types ;

separate_table_types:
	# Effect: Distinction between
	#         - base, temporary, merge and partioned tables + views
	#         - tables of any type and functions,procedures,triggers,events
	#         Only statements which are applicable to this type of table will be generated.
	#         Example: ALTER VIEW <existing partitioned table> ... should be not generated.
	# Advantage: Less failing statements, logs are much easier to read
	# Disadvantage: The avoided suitations are not tested.
	{ $base_piece="base"; $temp_piece="temp"; $merge_piece="merge"; $part_piece="part"; $fkey_piece="fkey"; $view_piece="view"; return undef};
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
	{ $base_piece="" ; $temp_piece="" ; $merge_piece="" ; $part_piece="" ; $fkey_piece="" ; $view_piece="" ; return undef } ;
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
	{$database_prefix="testdb"; $table_prefix="t1_"; $procedure_prefix="p1_"; $function_prefix="f1_"; $trigger_prefix="tr1_"; $event_prefix="e1_"; return undef};
no_separate_objects:
	# Effect: At least no distinction between functions, triggers, procedures and events
	#         If no_separate_table_types is added, than also tables are no more separated.
	#         Example: CALL <existing partitioned table> ... should be not generated.
	# Advantage: More coverage
	# Disadvantage: More failing statements
	{$database_prefix="o1_1"; $table_prefix="o1_"; $procedure_prefix="o1_"; $function_prefix="o1_"; $trigger_prefix="o1_"; $event_prefix="o1_"; return undef};

avoid_bugs:
	# Set this grammar rule to "empty" if for example no optimizer related server system variable has to be switched.
	;

event_scheduler_on:
	SET GLOBAL EVENT_SCHEDULER = ON ;

event_scheduler_off:
	SET GLOBAL EVENT_SCHEDULER = OFF ;

have_some_initial_objects:
	# It is assumed that this reduces the likelihood of "Table does not exist" significant when running with a small number of "worker" threads.
	# The amount of create_..._table rules within the some_..._tables should depend a bit on the value in $namespace_width but I currently
	# do not know how to express this in the grammar.
	some_databases ; some_base_tables ; some_temp_tables ; some_merge_tables ; some_part_tables ; some_view_tables ; some_functions ; some_procedures ; some_trigger ; some_events ;
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


# Useful grammar rules ====================================================================================#

rand_val:
	{ $rand_val = $prng->int(0,100) / 100 } ;


# Namespaces of objects ==========================================================================#
# An explanation of the namespace concept is on top of this file.
#
# 1. The database namespace ##########################################################################
database_name_s:
	{ $database_name_s = $database_prefix . $sequence_piece ; $database_name = $database_name_s };
database_name_n:
	{ $database_name_n = $database_prefix . $normal_piece   ; $database_name = $database_name_n };
database_name:
	# Get a random name from the "database" namespace.
	# $database_name gets automatically filled when database_name_s or database_name_n is executed.
	database_name_s | database_name_n ;


# 2. The base table namespace ########################################################################
base_table_name_s:
	# Get a random name from the "base table long life" namespace.
	{$base_table_name_s = $table_prefix.$base_piece.$prng->int(1,$namespace_width).$sequence_piece; $base_table_name = $base_table_name_s; $table_name = $base_table_name};
base_table_name_n:
	# Get a random name from the "base table short life" namespace.
	{$base_table_name_n = $table_prefix.$base_piece.$prng->int(1,$namespace_width).$normal_piece  ; $base_table_name = $base_table_name_n; $table_name = $base_table_name};
base_table_name:
	# Get a random name from the "base table" namespace.
	base_table_name_s | base_table_name_n ;

# Sometimes useful stuff:
base_table_item_s:
	database_name_s . base_table_name_s {$base_table_item_s = $database_name_s." . ".$base_table_name_s; $base_table_item = $base_table_item_s; return undef};
base_table_item_n:
	database_name   . base_table_name_n {$base_table_item_n = $database_name  ." . ".$base_table_name_n; $base_table_item = $base_table_item_n; return undef};
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
	{$temp_table_name_s = $table_prefix.$temp_piece.$prng->int(1,$namespace_width).$sequence_piece; $temp_table_name = $temp_table_name_s; $table_name = $temp_table_name};
temp_table_name_n:
	# Get a random name from the "temp table short life" namespace.
	{$temp_table_name_n = $table_prefix.$temp_piece.$prng->int(1,$namespace_width).$normal_piece  ; $temp_table_name = $temp_table_name_n; $table_name = $temp_table_name};
temp_table_name:
	# Get a random name from the "temp table" namespace.
	temp_table_name_s | temp_table_name_n ;

# Sometimes useful stuff:
temp_table_item_s:
	database_name_s . temp_table_name_s {$temp_table_item_s = $database_name_s." . ".$temp_table_name_s; $temp_table_item = $temp_table_item_s; return undef};
temp_table_item_n:
	database_name   . temp_table_name_n {$temp_table_item_n = $database_name  ." . ".$temp_table_name_n; $temp_table_item = $temp_table_item_n; return undef};
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
	{$merge_table_name_s = $table_prefix.$merge_piece.$prng->int(1,$namespace_width).$sequence_piece; $merge_table_name = $merge_table_name_s; $table_name = $merge_table_name};
merge_table_name_n:
	# Get a random name from the "merge table short life" namespace.
	{$merge_table_name_n = $table_prefix.$merge_piece.$prng->int(1,$namespace_width).$normal_piece  ; $merge_table_name = $merge_table_name_n; $table_name = $merge_table_name};
merge_table_name:
	# Get a random name from the "merge table" namespace.
	merge_table_name_s | merge_table_name_n ;

# Sometimes useful stuff:
merge_table_item_s:
	database_name_s . merge_table_name_s {$merge_table_item_s = $database_name_s." . ".$merge_table_name_s; $merge_table_item = $merge_table_item_s; return undef};
merge_table_item_n:
	database_name   . merge_table_name_n {$merge_table_item_n = $database_name  ." . ".$merge_table_name_n; $merge_table_item = $merge_table_item_n; return undef};
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
	{$view_table_name_s = $table_prefix.$view_piece.$prng->int(1,$namespace_width).$sequence_piece; $view_table_name = $view_table_name_s; $table_name = $view_table_name};
view_table_name_n:
	# Get a random name from the "view table short life" namespace.
	{$view_table_name_n = $table_prefix.$view_piece.$prng->int(1,$namespace_width).$normal_piece  ; $view_table_name = $view_table_name_n; $table_name = $view_table_name};
view_table_name:
	# Get a random name from the "view table" namespace.
	view_table_name_s | view_table_name_n ;

# Sometimes useful stuff:
view_table_item_s:
	database_name_s . view_table_name_s {$view_table_item_s = $database_name_s." . ".$view_table_name_s; $view_table_item = $view_table_item_s; return undef};
view_table_item_n:
	database_name   . view_table_name_n {$view_table_item_n = $database_name  ." . ".$view_table_name_n; $view_table_item = $view_table_item_n; return undef};
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
	{$part_table_name_s = $table_prefix.$part_piece.$prng->int(1,$namespace_width).$sequence_piece; $part_table_name = $part_table_name_s; $table_name = $part_table_name};
part_table_name_n:
	# Get a random name from the "part table short life" namespace.
	{$part_table_name_n = $table_prefix.$part_piece.$prng->int(1,$namespace_width).$normal_piece  ; $part_table_name = $part_table_name_n; $table_name = $part_table_name};
part_table_name:
	# Get a random name from the "part table" namespace.
	part_table_name_s | part_table_name_n ;

# Sometimes useful stuff:
part_table_item_s:
	database_name_s . part_table_name_s {$part_table_item_s = $database_name_s." . ".$part_table_name_s; $part_table_item = $part_table_item_s; return undef};
part_table_item_n:
	database_name   . part_table_name_n {$part_table_item_n = $database_name  ." . ".$part_table_name_n; $part_table_item = $part_table_item_n; return undef};
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
#     These grammar rules could be used to avoid some partioning or merge table related bugs.
base_temp_view_table_item_s:
	base_table_item_s | temp_table_item_s | view_table_item_s | part_table_item_s ;
base_temp_view_table_item_n:
	base_table_item_n | temp_table_item_n | view_table_item_n | part_table_item_n ;
base_temp_view_table_item:
	base_temp_view_table_item_s | base_temp_view_table_item ;

# 7.4 Similar table ################################################################
#     Fill the variables with the names of tables having the same type and and "normal" lifetime.
similar_table_item:
   /* base_table_item_n  */ {$similar_table_item1 = $base_table_item_n  ; return undef} /* base_table_item_n  */ {$similar_table_item2 = $base_table_item_n  ; return undef}|
   # A temporary table remains a temporary table even after renaming.
   /* temp_table_item_n  */ {$similar_table_item1 = $temp_table_item_n  ; return undef} /* temp_table_item_n  */ {$similar_table_item2 = $temp_table_item_n  ; return undef}|
   /* merge_table_item_n */ {$similar_table_item1 = $merge_table_item_n ; return undef} /* merge_table_item_n */ {$similar_table_item2 = $merge_table_item_n ; return undef}|
   /* part_table_item_n  */ {$similar_table_item1 = $part_table_item_n  ; return undef} /* part_table_item_n  */ {$similar_table_item2 = $part_table_item_n  ; return undef};



# 8. Other namespaces ##############################################################
template_table_item:
	# The disabled names are for future use. They cannot work with the current properties of .zz grammars.
	# The problem is that we get in some scenarios tables with differing numnber of columns.
	# { $template_table_item = "test.table0" }              |
	# { $template_table_item = "test.table1" }              |
	# { $template_table_item = "test.table10" }             |
	{ $template_table_item = "test.table100_int" }         |
	{ $template_table_item = "test.table10_int" }          |
	{ $template_table_item = "test.table1_int" }           |
	{ $template_table_item = "test.table0_int" }           |
	{ $template_table_item = "test.table100_int_autoinc" } |
	{ $template_table_item = "test.table10_int_autoinc" }  |
	{ $template_table_item = "test.table1_int_autoinc" }   |
	{ $template_table_item = "test.table0_int_autoinc" }   ;


procedure_name_s:
	# Get a random name from the "procedure long life" namespace.
	{$procedure_name_s = $procedure_prefix.$prng->int(1,$namespace_width).$sequence_piece; $procedure_name = $procedure_name_s};
procedure_name_n:
	# Get a random name from the "procedure short life" namespace.
	{$procedure_name_n = $procedure_prefix.$prng->int(1,$namespace_width).$normal_piece  ; $procedure_name = $procedure_name_n};
procedure_name:
	# Get a random name from the "procedure" namespace.
	procedure_name_s | procedure_name_n ;

# Sometimes useful stuff:
procedure_item_s:
	database_name_s . procedure_name_s {$procedure_item_s = $database_name_s." . ".$procedure_name_s; $procedure_item = $procedure_item_s; return undef};
procedure_item_n:
	database_name   . procedure_name_n {$procedure_item_n = $database_name  ." . ".$procedure_name_n; $procedure_item = $procedure_item_n; return undef};
procedure_item:
	procedure_item_s | procedure_item_n ;

function_name_s:
	# Get a random name from the "function long life" namespace.
	{$function_name_s  = $function_prefix.$prng->int(1,$namespace_width).$sequence_piece; $function_name = $function_name_s};
function_name_n:
	# Get a random name from the "function short life" namespace.
	{$function_name_n  = $function_prefix.$prng->int(1,$namespace_width).$normal_piece  ; $function_name = $function_name_n};
function_name:
	# Get a random name from the "function" namespace.
	function_name_s | function_name_n ;

function_item_s:
	database_name_s . function_name_s {$function_item_s = $database_name_s." . ".$function_name_s; $function_item = $function_item_s; return undef};
function_item_n:
	database_name   . function_name_n {$function_item_n = $database_name  ." . ".$function_name_n; $function_item = $function_item_n; return undef};
function_item:
	function_item_s | function_item_n ;

trigger_name_s:
	# Get a random name from the "trigger long life" namespace.
	{$trigger_name_s = $trigger_prefix.$prng->int(1,$namespace_width).$sequence_piece; $trigger_name = $trigger_name_s};
trigger_name_n:
	# Get a random name from the "trigger short life" namespace.
	{$trigger_name_n = $trigger_prefix.$prng->int(1,$namespace_width).$normal_piece  ; $trigger_name = $trigger_name_n};
trigger_name:
	# Get a random name from the "trigger" namespace.
	trigger_name_s | trigger_name_n ;

trigger_item_s:
	database_name_s . trigger_name_s {$trigger_item_s = $database_name_s." . ".$trigger_name_s; $trigger_item = $trigger_item_s; return undef};
trigger_item_n:
	database_name   . trigger_name_n {$trigger_item_n = $database_name  ." . ".$trigger_name_n; $trigger_item = $trigger_item_n; return undef};
trigger_item:
	trigger_item_s | trigger_item_n ;

event_name_s:
	# Get a random name from the "event long life" namespace.
	{$event_name_s = $event_prefix.$prng->int(1,$namespace_width).$sequence_piece; $event_name = $event_name_s};
event_name_n:
	# Get a random name from the "event short life" namespace.
	{$event_name_n = $event_prefix.$prng->int(1,$namespace_width).$normal_piece  ; $event_name = $event_name_n};
event_name:
	# Get a random name from the "event" namespace.
	event_name_s | event_name_n ;

event_item_s:
	database_name_s . event_name_s {$event_item_s = $database_name_s." . ".$event_name_s; $event_item = $event_item_s; return undef};
event_item_n:
	database_name   . event_name_n {$event_item_n = $database_name  ." . ".$event_name_n; $event_item = $event_item_n; return undef};
event_item:
	event_item_s | event_item_n ;

# Here starts the core of the test grammar ========================================================#

query:
	dml  | dml  | dml  | dml  |
   transaction | transaction |
   lock_unlock | lock_unlock |
   ddl                       |
   flush                     |
   handler                   ;

########## TRANSACTIONS ####################

transaction:
	start_transaction | commit | rollback |
	start_transaction | commit | rollback |
	start_transaction | commit | rollback |
	SAVEPOINT savepoint_id | RELEASE SAVEPOINT savepoint_id | ROLLBACK work_or_empty TO savepoint_or_empty savepoint_id |
	BEGIN work_or_empty | set_autocommit | kill_query_or_session |
	set_isolation_level ;

savepoint_id:
   A |
   B ;

set_isolation_level:
	SET SESSION TX_ISOLATION = TRIM(' isolation_level ');

isolation_level:
	READ-UNCOMMITTED |
	READ-COMMITTED   |
	REPEATABLE-READ  |
	SERIALIZABLE     ;


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
# 3. S1 tries to kill S3 which already does no more exist.
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
	merge_table_ddl | merge_table_ddl | merge_table_ddl |
	part_table_ddl  | part_table_ddl  | part_table_ddl  |
	view_ddl        | view_ddl        | view_ddl        |
	procedure_ddl   | procedure_ddl   | procedure_ddl   |
	function_ddl    | function_ddl    | function_ddl    |
	trigger_ddl     | trigger_ddl     | trigger_ddl     |
	event_ddl       |
	truncate_table  |
	drop_table_list |
	rename_table    |
	# Bug#54486 assert in my_seek, concurrent DROP/CREATE SCHEMA, CREATE TABLE, REPAIR
	# affects table_maintenance_ddl in mysql-5.1.
	# The problem has disappeared in higher MySQL versions.
	table_maintenance_ddl      |
	dump_load_data_sequence    |
	grant_revoke               |
   rare_ddl                   |
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
	HANDLER table_no_view_item OPEN as_or_empty handler_a ;

handler_read:
	HANDLER handler_a READ handler_index comparison_operator ( _digit ) handler_read_part |
	HANDLER handler_a READ handler_index first_next_prev_last           handler_read_part |
	HANDLER handler_a READ               first_next                     handler_read_part ;
handler_index:
   # Attention: `PRIMARY` means use the PRIMARY KEY and the backticks around PRIMARY are
   #            mandatory (I guess PRIMARY is a key word).
	`PRIMARY`  |
	index_name ;
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
	is_debug1 is_debug2 { return $m1 } SHOW FUNCTION CODE function_item { return $m2 };

show_function_status:
	SHOW FUNCTION STATUS ;

show_create_procedure:
	SHOW CREATE PROCEDURE procedure_item ;

show_procedure_code:
	is_debug1 is_debug2 { return $m1 } SHOW PROCEDURE CODE procedure_item { return $m2 };

is_debug1:
	# Calculate if we have a debug server.
	{$is_debug_server = -1; open($my_file,'<'.$out_file); read($my_file,$is_debug_server,1000); close($my_file); return undef};

is_debug2:
	# Set the marker according if we have a debug server or not.
	{ $m1='/*'; $m2='*/'; if ( $is_debug_server == 1 ) { $m1=''; $m2='' }; return undef } ;

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

database_spec:
	# We do not want to test CHARACTER SETs and COLLATIONs here,
   # but we need something for ALTER DATABASE.
   default_or_empty CHARACTER SET equal_or_empty utf8 |
   default_or_empty COLLATE equal_or_empty utf8_bin   ;

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
   drop_base_table       |
   alter_base_temp_table |
   base_table_sequence   |
   create_fkey_table     |
   fkey_table_sequence   ;

create_base_table:
	CREATE TABLE if_not_exists base_table_item_n { $create_table_item = $base_table_item_n ; return undef } create_table_part ;
create_table_part:
	LIKE template_table_item ; ALTER TABLE $create_table_item ENGINE = engine ; INSERT INTO $create_table_item SELECT * FROM $template_table_item |
	LIKE template_table_item ; ALTER TABLE $create_table_item ENGINE = engine ; INSERT INTO $create_table_item SELECT * FROM $template_table_item |
	AS used_select           ;

drop_base_table:
	# DROP two tables is in "drop_table_list"
	DROP TABLE if_exists base_table_item_n restrict_cascade ;


alter_base_temp_table:
   ALTER ignore TABLE similar_table_item $similar_table_item1 alter_base_temp_table_part alter_algorithm alter_concurrency ;

alter_base_temp_table_part:
   # Reasons why some "ALTERs" are rare:
   # 1. Some are rather rare within a production system running under DML load
   # 2. Some are not supported by the storage engine used
   # 3. Some damage tables.
   #    Example:
   #       The MERGE table merge_tab consists of the MyISAM tables base_tab1 and base_tab2.
   #       ALTER TABLE base_tab1 ENGINE = <engine != MyISAM> might have success.
   #       Now the MERGE table merge_tab is damaged because nearly all statements
   #       on merge_tab will fail.
   #    As a consequence nearly all statements on the MERGE table will fail.
   alter_comment | alter_comment | alter_comment | alter_comment | alter_comment | alter_comment |
   alter_engine                                                                                  |
   alter_extra_column                                                                            |
   alter_column                                                                                  |
   alter_rename_table                                                                            |
   alter_index_pk | alter_index_pk                                                               |
   alter_base_temp_table_part, alter_base_temp_table_part                                        ;

alter_comment:
   COMMENT = "UPDATED to _digit " ;

alter_engine:
   ENGINE = engine                ;

alter_index_pk:
   # 1/3 of all base tables created via this grammar are generated via
   #    CREATE TABLE ... AS SELECT template_table_item.
   # -> ~ 1/3 of all early DROP PRIMARY KEY attempts will fail.
   # -> A bit less than 1/3 early ADD PRIMARY KEY attempts should have success.
   #    The exceptions are: Duplicate columns within the PRIMARY KEY column list,
   #                        non unique content of intended PRIMARY KEY column list
   # 2/3 of all base table created via this grammar are generated via
   #    CREATE TABLE ... LIKE template_table_item
   # The template tables are create via the concurrency_1.zz grammar.
   # All template tables have a PRIMARY KEY `pk`.
   # 1/2 of the template tables have
   #    `pk` INTEGER AUTO_INCREMENT, PRIMARY KEY `pk`.
   # -> ~ 1/6 of all early DROP PRIMARY KEY attempts will fail.
   # -> A bit more than ~ 2/3 of all early ADD PRIMARY KEY attempts will fail.
   # In order to limit the resistance of the table group with
   #    `pk` INTEGER AUTO_INCREMENT, PRIMARY KEY `pk`
   # one alternative in the rule "alter_column" removes the default AUTO_INCREMENT.
   ADD  add_acceleration_structure  field_list ( $ifield_list ) |
   DROP drop_acceleration_structure                             ;

index_name:
   idx1 |
   idx2 ;

alter_extra_column:
   ADD  COLUMN extra INTEGER DEFAULT 13 |
   DROP COLUMN extra                    ;

alter_column:
   # Changing some column from whatever data type to INT and back is maybe not ideal.
   # But fortunately the lifetime of this table is anyway short.
   CHANGE COLUMN column_to_change my_column INT |
   CHANGE COLUMN my_column column_to_change INT |
   # Change the name but not the data type.
   CHANGE COLUMN col_int my_col_int INTEGER     |
   CHANGE COLUMN my_col_int col_int INTEGER     |
   # Change the column default
   MODIFY COLUMN col_int INTEGER DEFAULT 13     |
   MODIFY COLUMN col_int INTEGER DEFAULT NULL   |
   MODIFY COLUMN pk      INTEGER                |
   # Change the data type but not the name.
   CHANGE COLUMN col_int col_int BIGINT         |
   CHANGE COLUMN col_int col_int INTEGER        ;

column_to_change:
   field { return $field };

alter_rename_table:
   RENAME TO $similar_table_item2 ;

field_list:
   # This routine
   # - fills two PERL variables
   #   - $field_list
   #     The content is Comma separated list of column names.
   #     This variable is to be used in FOREIGN KEY relations.
   #   - $ifield_list
   #     Comma separated list of column name + optional prefix length + optional sorting direction.
   #     This variable is to be used in statements which create primary keys or indexes.
   # - does not return anything.
   # The amount of elements within the lists is random.
   # The preceding "{ undef <variable name> }" ensures that we get rid of the data from any
   # previous call of the rule "field_list".
   { undef $field_list ; undef $ifield_list ; return undef } field_list_build ;

field_list_build:
   field ifield_len ifield_dir {$fl_o = $field_list; $field_list = $fl_o.' '.$field    ; $ifl_o = $ifield_list; $ifield_list = $ifl_o.' '.$field.$ifield_len.$ifield_dir    ; return undef}                  |
   field ifield_len ifield_dir {$fl_o = $field_list; $field_list = $fl_o.' '.$field.','; $ifl_o = $ifield_list; $ifield_list = $ifl_o.' '.$field.$ifield_len.$ifield_dir.','; return undef} field_list_build ;

field:
   # Attention: The definition of this rule must fit to the tables created
   #            via concurrency_1.zz .
   { $field = 'pk'             ; return undef } |
   { $field = 'col_varchar_64' ; return undef } |
   { $field = 'col_float'      ; return undef } |
   { $field = 'col_int'        ; return undef } |
   { $field = 'col_decimal'    ; return undef } |
   { $field = 'col_blob'       ; return undef } ;

ifield_dir:
   { $ifield_dir = ''      ; return undef } |
   { $ifield_dir = ' ASC'  ; return undef } |
   { $ifield_dir = ' DESC' ; return undef } ;

ifield_len:
   # From the manual regarding indexes:
   #    Prefixes can be specified for CHAR, VARCHAR, BINARY, and VARBINARY columns.
   #    BLOB and TEXT columns also can be indexed, but a prefix length must be given.
   # Using a prefix length for blob columns only should be sufficient for covering
   # such kind of primary keys and indexes.
   # 1000 Byte (+ there might be more columns in the structure) might be too long
   # for the capabilities of soem storage engine --> 1071 ER_TOO_LONG_KEY
   { if ( $field =~ m{blob} ) { $ifield_len = '(' . $prng->int(1,1000) . ')' } else { $ifield_len = '' }; return undef };

alter_algorithm:
                                                    |
   , ALGORITHM equal_or_empty alter_algorithm_value ;

alter_algorithm_value:
   DEFAULT |
   INPLACE |
   COPY    ;

alter_concurrency:
                                                 |
   , LOCK equal_or_empty alter_concurrency_value ;

alter_concurrency_value:
   DEFAULT   |
   NONE      |
   SHARED    |
   EXCLUSIVE ;

base_table_sequence:
	$sequence_begin CREATE TABLE if_not_exists base_table_item_s LIKE template_table_item ; ALTER TABLE $base_table_item_s ENGINE = engine ; INSERT INTO $base_table_item_s SELECT * FROM $template_table_item ; COMMIT ; wait_till_drop_table ; DROP TABLE $base_table_item_s $sequence_end ;

wait_till_drop_table:
	SELECT SLEEP( rand_val * $life_time_unit ) ;

temp_table_ddl:
	# Attention:
   # 1. temp_table_sequence is intentionally omitted, because no other session will be
	#    able to use this table.
   # 2. Altering a temporary table is in alter_base_temp_table.
	create_temp_table | create_temp_table | create_temp_table | create_temp_table | create_temp_table | create_temp_table |
	drop_temp_table   ;

create_temp_table:
	CREATE TEMPORARY TABLE if_not_exists temp_table_item_n {$create_table_item = $temp_table_item_n; return undef} create_table_part ;

drop_temp_table:
	# DROP two tables is in "drop_table_list"
	# A pure DROP TABLE is allowed, but we get an implicit COMMITs for that.
	DROP TEMPORARY TABLE if_exists temp_table_item_n |
	DROP           TABLE if_exists temp_table_item_n ;

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
	REPAIR not_to_binlog_local TABLE table_list quick extended use_frm ;

use_frm:
	# Only 10 %
	| | | | | | | |  | USE_FRM ;


########## MIXED TABLE RELATED DDL #################################
truncate_table:
   TRUNCATE table_or_empty table_no_view_item_n ;

drop_table_list:
   # DROP one table is in "drop_*table"
   # 1. We mix here all tables except VIEWs up.
   # 2. We have an increased likelihood that the statement fails because of use of
   #    - "temporary" (only correct in case of a temporary table)
   #    - two tables (some might not exist)
   DROP temporary TABLE if_exists table_no_view_item_n , table_no_view_item_n restrict_cascade ;

rename_table:
   # RENAME TABLE works also on all types of tables (includes VIEWs).
   # We try this within the VIEW section.
   RENAME TABLE rename_item_list ;
rename_item_list:
   rename_item | rename_item , rename_item ;
rename_item:
   # The rule similar_table_item serves to preserve the object type (base,temp,....)
   # and type (Normal) otherwise debugging becomes difficult and
   # the concept with different lifetimes gets broken.
   similar_table_item $similar_table_item1 TO $similar_table_item2 ;


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
	# There is a significant likelihood that random picked table names as base for the merge table cannot
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
	# It is intentional that we use merge_table_name and not merge_table_name_n.
	ALTER ignore TABLE merge_table_item_n COMMENT = 'UPDATED to _digit '      |
	ALTER        TABLE merge_table_item_n INSERT_METHOD = insert_method_value ;

merge_init_s:
	/* merge_table_item_s {$mt = $merge_table_item_s; return undef} consists of ( base_table_item_s {$mp1 = $base_table_item_s; return undef} , base_table_item_s {$mp2 = $base_table_item_s; return undef} ) based on template_table_item */ ;
merge_init_n:
	/* merge_table_item_n {$mt = $merge_table_item_n; return undef} consists of ( base_table_item_n {$mp1 = $base_table_item_n; return undef} , base_table_item_n {$mp2 = $base_table_item_n; return undef} ) based on template_table_item */ ;
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
	CREATE TABLE if_not_exists part_table_item_n ENGINE = engine_for_part partition_algorithm AS SELECT * FROM template_table_item |
	CREATE TABLE if_not_exists part_table_item_n ENGINE = engine_for_part partition_algorithm AS SELECT * FROM template_table_item |
	CREATE TABLE if_not_exists part_table_item_n ENGINE = engine_for_part partition_algorithm AS used_select                       ;

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
	partition_algorithm            |
	COMMENT = 'UPDATED to _digit ' ;

part_table_sequence:
	$sequence_begin CREATE TABLE if_not_exists part_table_item_s ENGINE = engine_for_part partition_algorithm AS SELECT * FROM template_table_item ; COMMIT ; wait_till_drop_table ; DROP TABLE $part_table_item_s $sequence_end ;


########## FOREIGN KEY relation ####################
# http://dev.mysql.com/doc/refman/5.6/en/innodb-foreign-key-constraints.html
# - FOREIGN KEYs are supported by InnDB only.
# - Corresponding columns in the foreign key and the referenced key must have similar internal data
#   types inside InnoDB so that they can be compared without a type conversion.
#   The size and sign of integer types must be the same.
#   The length of string types need not be the same.
#   For nonbinary (character) string columns, the character set and collation must be the same.
# - InnoDB requires indexes on foreign keys and referenced keys...
#   In case the index on foreign keys is missing, it will be generated automatically.
# - In the referencing table, there must be an index where the foreign key columns are listed as
#   the first columns in the same order.
# - However, in the referenced table, there must be an index where the referenced columns are listed
#   as the first columns in the same order.
# What could/should be generated via randomness:
#    CREATE TABLE if_not_exists $fk_item2 ENGINE = InnoDB AS SELECT * FROM  $fk_item1 ;
#    ALTER TABLE $fk_item1 ADD add_acceleration_structure field_list ( $ifield_list ) ;
#    ALTER TABLE $fk_item2 ADD CONSTRAINT fk1 FOREIGN KEY ( $field_list ) REFERENCES $fk_item1 ( $field_list ) nothing_delete_update;
# 1. column_list1 == column_list2
#    I hope that omitting the case column_list1 <> column_list2 (statement will fail)
#    does not limit the functional coverage too much.
# 2. In case the referencing table $fk_item2 has
#    - already some index which fits to the columnlist (this is IMHO not that likely
#      but not impossible) than this indexe will be used by the server.
#      -> Column list used for foreign key equals the column list of the fitting index.
#      -> Column list used for foreign key is a sub set of the column list of the fitting index.
#    - no index which fits to the columnlist than the server tries to creates some index which
#      fits automatically
#      Case that some existing index has the same name like the CONSTRAINT
#      -> the statement fails
#      Case that no existing index has the same name like the CONSTRAINT
#      -> Column list used for foreign key equals the column list of the fitting index.
# 3. The possible states for the referenced table $fk_item1 are similar with the
#    exception that the required index will be not created automatically.
#

create_fkey_table:
   # 1. The attempt to create the FOREIGN KEY is in cr_fk5.
   # 2. There are a lot requirements
   #    - the base tables must exist
   #    - the tables must use the storage engine InnoDB
   #    - the tables must be no temporary tables
   #    - there must be sufficient indexes on the tables
   #    which must be fulfilled in order to have success
   #    in creating the FOREIGN KEY.
   #    cr_fk1 till cr_fk3 are not required for having
   #    some success at all but they increase the likelihood
   #    to have success.
   # We do not need cr_fk4 here because the required INDEX
   # will be created automatically.
   fkey_init_n  cr_fk1  ; cr_fk2  ; cr_fk3 ; cr_fk5 |
   fkey_init_n  cr_fk1  ; cr_fk2  ; cr_fk3 ; cr_fk5 |
   fkey_init_pn cr_fk1p ; cr_fk2p ; cr_fk3 ; cr_fk5 ;
fkey_table_sequence:
	# Attention:
   # A DROP TABLE is not included because I assume that the rules "database_sequence" and
   # "base_table_sequence" remove the tables frequent enough.
   $sequence_begin fkey_init_s  cr_fk1  ; cr_fk2  ; cr_fk3 ; cr_fk4 ; cr_fk5 ; wait_till_drop_table ; cr_fk6 $sequence_end |
   $sequence_begin fkey_init_s  cr_fk1  ; cr_fk2  ; cr_fk3 ; cr_fk4 ; cr_fk5 ; wait_till_drop_table ; cr_fk6 $sequence_end |
   $sequence_begin fkey_init_ps cr_fk1p ; cr_fk2p ; cr_fk3 ; cr_fk4 ; cr_fk5 ; wait_till_drop_table ; cr_fk6 $sequence_end ;

fkey_init_n:
   /* base_table_item_n {$fk_item1 = $base_table_item_n; return undef} base_table_item_n {$fk_item2 = $base_table_item_n; return undef} */ ;
fkey_init_pn:
   /* part_table_item_n {$fk_item1 = $part_table_item_n; return undef} part_table_item_n {$fk_item2 = $part_table_item_n; return undef} */ ;
fkey_init_s:
   /* base_table_item_s {$fk_item1 = $base_table_item_s; return undef} base_table_item_s {$fk_item2 = $base_table_item_s; return undef} */ ;
fkey_init_ps:
   /* part_table_item_s {$fk_item1 = $part_table_item_s; return undef} part_table_item_s {$fk_item2 = $part_table_item_s; return undef} */ ;

cr_fk1:
   CREATE TABLE if_not_exists $fk_item1 ENGINE = InnoDB                     AS SELECT * FROM  template_table_item ;
cr_fk1p:
   CREATE TABLE if_not_exists $fk_item1 ENGINE = InnoDB partition_algorithm AS SELECT * FROM  template_table_item ;
cr_fk2:
   CREATE TABLE if_not_exists $fk_item2 ENGINE = InnoDB                     AS SELECT * FROM  $fk_item1 ;
cr_fk2p:
   CREATE TABLE if_not_exists $fk_item2 ENGINE = InnoDB partition_algorithm AS SELECT * FROM  $fk_item1 ;

cr_fk3:
   ALTER ignore TABLE $fk_item1 ADD add_acceleration_structure field_list ( $ifield_list ) alter_algorithm alter_concurrency ;
# partition_algorithm
cr_fk4:
   ALTER ignore TABLE $fk_item2 ADD add_acceleration_structure            ( $ifield_list ) alter_algorithm alter_concurrency ;
cr_fk5:
   # In case of success the FOREIGN KEY gets the name foreign_key_name.
   # In case of success + some already on $fk_item2 existing index fits
   # the name of this index will be not modified.
   # In case of success + none of the already on $fk_item2 existing indexes fits
   # some additional index with the name foreign_key_name will be generated automatically.
   ALTER TABLE $fk_item2 ADD CONSTRAINT foreign_key_name FOREIGN KEY ( $field_list ) REFERENCES $fk_item1 ( $field_list ) nothing_delete_update ;
   # There is also some possible variant where no CONSTRAINT name is assigend
   #    ALTER TABLE $fk_item2 ADD FOREIGN KEY idx_name (col1) REFERENCES t1 (col1) ON ...
   # Here the system invents some name like '<table name>_ibfk_<number>' for the FOREIGN KEY
   # and some index with the name idx_name will be created in case none of the existing
   # indexes fits. We do not test this here.
cr_fk6:
   ALTER ignore TABLE $fk_item1 DROP FOREIGN KEY $foreign_key_name alter_algorithm alter_concurrency ;

foreign_key_name:
   # 1. FOREIGN KEY names are unique per database.  So some not too small name space is needed.
   # 2. Under certain conditions some index with the same name will be created.
   #    In order to allow sporadic clashes like
   #       There exists already an index with this name and this index
   #       cannot be used for the FOREIGN KEY.
   #       --> Statement fails
   #    we make the FOREIGN KEY name space some super set of the index name space.
	{ $foreign_key_name = "idx" . $prng->int(0,10) } ;

add_acceleration_structure:
           index_or_key index_name |
           index_or_key index_name |
   UNIQUE  index_or_key index_name |
   PRIMARY KEY                     ;

drop_acceleration_structure:
           index_or_key index_name |
           index_or_key index_name |
           index_or_key index_name |
   PRIMARY KEY                     ;

nothing_delete_update:
   # nothing specified -> the default action is RESTRICT.
                                                         |
   ON DELETE reference_option                            |
   ON DELETE reference_option                            |
   ON UPDATE reference_option                            |
   ON UPDATE reference_option                            |
   ON DELETE reference_option ON UPDATE reference_option |
   ON UPDATE reference_option ON DELETE reference_option ;

reference_option:
   RESTRICT  |
   CASCADE   |
   SET NULL  |
   NO ACTION ;

########## RARE DDL ####################
# This rule is dedicated to DDL statements which
# - are rather unlikely
# - have a high likelihood to be rejected by the server
rare_ddl:
	| | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | |
   # Altering enabled log tables is disallowed -> ER_BAD_LOG_STATEMENT
   # It is only in case "--mysqld=--general_log=0" allowed.
   ALTER TABLE mysql . general_log alter_extra_column                    |
   # Switch simply every dynamic Innodb related parameter
   # --------------------------------------------------------------
   # SET GLOBAL innodb_adaptive_flushing   = zero_or_one_or_default        |
   # SET GLOBAL innodb_adaptive_hash_index = zero_or_one_or_default        |
   # Introduced in 5.6.3, microseconds, 0 till 1000000, default is 0
   # SET GLOBAL innodb_adaptive_max_sleep_delay = max_sleep_delay_val      |
   # Introduced in 5.6.2, default is 0
   # SET GLOBAL innodb_analyze_is_persistent = zero_or_one_or_default      |
   # Buffer pool dum/load/... Introduced in 5.6.3
   # default is 1
   # SET GLOBAL innodb_buffer_pool_dump_now = zero_or_one_or_default       |
   # default is 1
   # SET GLOBAL innodb_buffer_pool_load_now  = zero_or_one_or_default      |
   # FIXME: Which filename?
   # SET GLOBAL innodb_buffer_pool_filename = ??????????????????????       |
   # default is 1 !
   # SET GLOBAL innodb_buffer_pool_load_abort = zero_or_one_or_default     |
   # Introduced in 5.6.2, 0 till 50, default 25
   # SET GLOBAL innodb_change_buffer_max_size = change_buffer_max_size_val |
   # default all
   # SET GLOBAL innodb_change_buffering = change_buffering_val             |
   # 1 till 4 Gi, default 500
   # SET GLOBAL innodb_concurrency_tickets  = concurrency_tickets_val      |
   # SET GLOBAL innodb_file_format = file_format_val                       |
   # default is 1
   # SET GLOBAL innodb_flush_log_at_trx_commit  = zero_one_two_or_default  |
   # Introduced in 5.6.3, default 1
   # SET GLOBAL innodb_flush_neighbors = zero_or_one_or_default            |
   # FIXME: Add the missing
   SET GLOBAL innodb_file_per_table = zero_or_one                        ;

max_sleep_delay_val:
   DEFAULT | DEFAULT | DEFAULT |
   DEFAULT | DEFAULT | DEFAULT |
   DEFAULT | DEFAULT | DEFAULT |
   { $prng->int(0,1000000) }   ;

change_buffer_max_size_val:
   DEFAULT | DEFAULT | DEFAULT |
   DEFAULT | DEFAULT | DEFAULT |
   DEFAULT | DEFAULT | DEFAULT |
   { $prng->int(0,50) }        ;

change_buffering_val:
   DEFAULT | DEFAULT | DEFAULT |
   DEFAULT | DEFAULT | DEFAULT |
   DEFAULT | DEFAULT | DEFAULT |
   ALL                         |
   INSERTS                     |
   DELETES                     |
   PURGES                      |
   CHANGES                     |
   NONE                        ;

concurrency_tickets_val:
   DEFAULT | DEFAULT | DEFAULT |
   DEFAULT | DEFAULT | DEFAULT |
   DEFAULT | DEFAULT | DEFAULT |
   { $prng->int(1,4290000000) };

file_format_val:
   DEFAULT   |
   Antelope  |
   Barracuda ;

zero_one_two_or_default:
   DEFAULT |
         0 |
         1 |
         2 ;


########## VIEW DDL ####################
view_ddl:
	create_view   | create_view | create_view | create_view | create_view | create_view | create_view | create_view |
	drop_view     | alter_view  | rename_view |
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

alter_view:
	# Attention: Only changing the algorithm is not allowed.
	ALTER ALGORITHM = view_algoritm VIEW view_table_item_n AS used_select ;

rename_view:
	RENAME TABLE view_table_item_n TO view_table_item_n ;

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
	ALTER PROCEDURE procedure_item_n COMMENT 'UPDATED to _digit ' ;

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
	ALTER FUNCTION function_item_n COMMENT 'UPDATED to _digit ' ;

function_sequence:
	$sequence_begin CREATE FUNCTION function_item_s () RETURNS INTEGER RETURN (SELECT MOD(COUNT(DISTINCT random_field_quoted1 ), 10) FROM table_item_s ) ; COMMIT ; SELECT wait_short ; DROP FUNCTION $function_item_s $sequence_end ;

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
	INSERT | DELETE | UPDATE ;
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
	ALTER EVENT event_item_s COMMENT 'UPDATED to _digit ';

########## DML ####################

dml:
	# Have only 10 % prepared statements.
	#    SQL Statements to be handled via PREPARE, EXECUTE and DEALLOCATE cause a bigger amount of
	#    failing statements than SQL statements which are executed in non prepared mode.
	#    The reason is that we run the EXECUTE and DEALLOCATE independent of the outcome of the
	#    PREPARE. So if the PREPARE fails because some table is missing, we loose the old
	#    prepared statement handle, if there was any, and get no new one. Therefore the succeeding
	#    EXECUTE and DEALLOCATE will also failcw because of missing statement handle.
	dml2 | dml2 | dml2 | dml2 | dml2 | dml2 | dml2 | dml2 | dml2     |
	PREPARE st1 FROM " dml2 " ; EXECUTE st1 ; DEALLOCATE PREPARE st1 ;

dml2:
	select | select | select  |
	do     | insert | replace | delete | update | CALL procedure_item | show | is_selects ;

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
	SELECT high_priority cache_results table_field_list_or_star FROM table_in_select as_or_empty A ;

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
	( SELECT table_field_list_or_star FROM table_item )            ;

addition:
	# Involve one (simple where condition) or two tables (subquery | join | union)
	where procedure_analyze       |
	subquery procedure_analyze    |
	join where procedure_analyze  |
	procedure_analyze union where ;

addition_no_procedure:
	# Involve one (simple where condition) or two tables (subquery | join | union)
	# Don't add procedure_analyze.
	where | where | where | where | where | where | where |
	subquery    |
	join where  |
	union where ;

where:
	# The very selective condition is intentional.
	# It should ensure that
	# - result sets (just SELECT) do not become too big because this affects the performance in general and
	#   the memory consumption of RQG (I had a ~ 3.5 GB virt memory RQG perl process during some simplifier run!)
	# - tables (INSERT ... SELECT, REPLACE) do not become too big
	# - tables (DELETE) do not become permanent empty
	# Please note that there are some cases where LIMIT cannot be used.
	WHERE `pk` BETWEEN _digit[invariant] AND _digit[invariant] + 1 |
	WHERE function_item () = _digit AND `pk` = _digit              ;


union:
	UNION SELECT $table_field_list FROM table_in_select as_or_empty B ;

join:
	# Do not place a where condition here.
	NATURAL JOIN table_item B  ;

subquery:
	correlated     |
	non_correlated ;
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
	| | | | | | | | |
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
	INSERT low_priority_delayed_high_priority ignore into_or_empty table_item simple_or_complicated on_duplicate_key_update ;
simple_or_complicated:
	( random_field_quoted1 ) VALUES ( digit_or_null ) |
	braced_table_field_list used_select LIMIT 1       ;
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
	REPLACE low_priority_delayed into_or_empty table_item simple_or_complicated ;


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
	SHOW GRANTS FOR otto@localhost                                                       ;

########## SQL MODE ########################
sql_mode:
	empty_mode | empty_mode | empty_mode | empty_mode |
	empty_mode | empty_mode | empty_mode | empty_mode |
	empty_mode | empty_mode | empty_mode | empty_mode |
	traditional_mode                                  ;
empty_mode:
	SET SESSION SQL_MODE = '' ;
traditional_mode:
	SET SESSION SQL_MODE = LOWER('TRADITIONAL');


########## DELETE ####################
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
   where    |
   subquery ;
delete_with_sleep:
	DELETE low_priority quick       FROM table_item      WHERE   `pk` + wait_short = _digit ;


########## UPDATE ####################
update:
	update_normal | update_normal | update_normal | update_normal | update_with_sleep ;
update_normal:
	UPDATE low_priority ignore table_item SET random_field_quoted1 = _digit WHERE `pk` > _digit LIMIT _digit                |
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
	READ local_or_empty |
	low_priority WRITE  ;

unlock:
	UNLOCK TABLES ;


########## FLUSH ####################
flush:
	# WITH READ LOCK causes that nearly all following statements will fail with
	#    Can't execute the query because you have a conflicting read lock
	# Therefore it should
	# - be rare
	# - last only a very short time
	# So I put it into a sequence with FLUSH ... ; wait a bit ; UNLOCK TABLES
	FLUSH TABLE table_list | FLUSH TABLE table_list | FLUSH TABLE table_list |
	FLUSH TABLE table_list | FLUSH TABLE table_list | FLUSH TABLE table_list |
	FLUSH TABLE table_list | FLUSH TABLE table_list | FLUSH TABLE table_list |
	FLUSH TABLE table_list | FLUSH TABLE table_list | FLUSH TABLE table_list |
	FLUSH TABLES           | FLUSH TABLES           | FLUSH TABLES           |
	FLUSH TABLES           | FLUSH TABLES           | FLUSH TABLES           |
   # The next line is set to comment but not removed because it is a very good
   # example for a logical mistake in the grammar.
   # Scenario(it really happened):
   #    The grammar simplifier already removed the
   #    - KILL <session> --> No chance that some other session kills some session
   #                         holding the READ LOCK.
   #    - the rule "lock_unlock" which contains UNLOCK TABLES
   #    - the last component from the current rule (contains UNLOCK)
   #    because he had success in replaying the desired effect.
   #    Now he tries to remove the UNLOCK TABLES from the next component
   #    which causes that we have
   #    - an enabled FLUSH TABLES table_list WITH READ LOCK
   #    - nowhere a UNLOCK TABLES or a KILL <session>
   #    ---> The RQG reporter "Deadlock" reported several times that it looks
   #         as if he has detected some "Deadlock".
   #         He is right but its a bug within the grammar and not within the server.
	# FLUSH TABLES table_list WITH READ LOCK | UNLOCK TABLES | UNLOCK TABLES   |
	FLUSH TABLES WITH READ LOCK ; SELECT wait_short ; UNLOCK TABLES          ;


########## TINY GRAMMAR ITEMS USED AT MANY PLACES ###########
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

digit_or_null:
	_digit | _digit | _digit | _digit | _digit | _digit | _digit | _digit | _digit |
	NULL ;

engine:
   # Storage engines to be used for "simple" base tables.
   InnoDB innodb_row_format |
   MyISAM                   |
   MEMORY                   ;

engine_for_part:
   # Storage engines to be used for partitioned base tables.
   MyISAM |
   InnoDB ;

innodb_row_format:
   |
   ROW_FORMAT = DEFAULT     |
   ROW_FORMAT = COMPACT     | # The default since 5.0.3
   ROW_FORMAT = REDUNDANT   | # The format prior to 5.0.3
   ROW_FORMAT = DYNAMIC     | # Requires file format Barracuda
   ROW_FORMAT = COMPRESSED  ; # Requires file format Barracuda and files per table


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
	field { return "'".$field."'" } ;

random_field_quoted1:
	field { return "`".$field."`" } ;

replace_option:
	# Only 20 % <> empty.
	| | | | REPLACE ;

restrict_cascade:
	# RESTRICT and CASCADE, if given, are parsed and ignored.
	| RESTRICT | CASCADE ;

sql_buffer_result:
	# Only 50%
	| SQL_BUFFER_RESULT ;

table_field_list_or_star:
	table_field_list | table_field_list | table_field_list | table_field_list |
	{ $table_field_list = "*" }                                               ;

table_field_list:
   # This generates a list with three elements and is used in SELECT, INSERT SELECT
   # FIXME: Would it make sense to generate lists with as many columns in list
   #        as we have columns in table?
	field {$table_field_list = $field; return undef} field {$table_field_list = $table_field_list.','.$field; return undef} field {$table_field_list = $table_field_list.','.$field};

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

zero_or_one:
	0 | 1 ;

zero_or_one_or_default:
	0 | 1 | DEFAULT ;

# Section with unimportant grammar rules ======================================#
# As soon as the parser accepts the statement variants which get generated via
# the alternatives within these rules, these alternatives start to be of rather
# low value. They do not increase the stress on the system significant.
# Therefore these rules
# - exist because the syntax generated should be rather complete
# - are excellent candidates for being simplified via for example redefine files
#   when running serious stress tests or grammar simplification.

as_or_empty:
      |
   AS ;

database_schema:
   DATABASE |
   SCHEMA   ;

databases_schemas:
   DATABASES |
   SCHEMAS   ;

default_or_empty:
           |
   DEFAULT ;

equal_or_empty:
     |
   = ;

index_or_key:
   KEY   |
   INDEX ;

into_or_empty:
        |
   INTO ;

savepoint_or_empty:
             |
   SAVEPOINT ;

table_or_empty:
         |
   TABLE ;

work_or_empty:
        |
   WORK ;


