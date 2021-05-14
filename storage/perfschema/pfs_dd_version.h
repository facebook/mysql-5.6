/* Copyright (c) 2017, 2022, Oracle and/or its affiliates.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2.0,
  as published by the Free Software Foundation.

  This program is also distributed with certain software (including
  but not limited to OpenSSL) that is licensed under separate terms,
  as designated in a particular file or component or in included license
  documentation.  The authors of MySQL hereby grant you an additional
  permission to link the program and your derivative works with the
  separately licensed software that they have included with MySQL.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License, version 2.0, for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#ifndef PFS_DD_VERSION_H
#define PFS_DD_VERSION_H

/**
  @file storage/perfschema/pfs_dd_version.h
  Performance schema data dictionary version.
*/

/**
  The version of the current performance_schema database schema.
  Version numbering forms a name space,
  which needs to be unique across all MySQL versions,
  even including forks.

  This version number is stored on disk in the data dictionary.
  Every time the performance_schema structure changes,
  this version number must change.

  Do not use a naive 1, 2, 3, ... N numbering scheme,
  as it requires an authoritative registry to assign numbers.
  This can not work in a distributed development environment,
  even less with forks, patches and back ports done by third parties.

  The numbering to use is the MySQL version number
  of the first MySQL version that published a given database schema.
  The format is Mmmdd with M=Major, m=minor, d=dot, and last three digits
  for Facebook specific schema changes so that MySQL 8.0.4 is encoded as 80004.

  In case of -dash version numbers, encode MySQL 8.12.34-56 as 8123456.

  Historical version number published in the data dictionary:

  1:

  Introduced in MySQL 8.0.3 by WL#7900.
  Never published in a GA version, abandoned.

  80004:

  performance_schema tables changed in MySQL 8.0.4 are
  - setup_threads (created)
  - setup_instruments (modified)
  - variables_info (modified)
  - setup_timers (removed)
  - metadata_locks (modified, added column COLUMN_NAME)
  - replication_connection_configuration (modified)
  - instance_log_resource (created)

  80005:

  performance_schema tables changed in MySQL 8.0.5 are
  - all, changed UTF8 (aka UTF8MB3) to UTF8MB4.

  80006:

  performance_schema tables changed in MySQL 8.0.6 are
  - variables_info.set_time precision changed from 0 to 6.

  80011:

  Version bump from 8.0.6 to 8.0.11,
  versions [8.0.5 - 8.0.10] inclusive are abandoned.
  performance_schema tables changed in MySQL 8.0.11 are
  - instance_log_resource was renamed to log_resource.

  80014:

  performance_schema tables changed in MySQL 8.0.14 are
  - events_statements_current, added column QUERY_ID
  - events_statements_history, added column QUERY_ID
  - events_statements_history_long, added column QUERY_ID

  80015:

  performance_schema.keyring_keys

  80017: -- WARNING, EXPOSED BY MySQL 8.0.16 --

  Unfortunately, MySQL 8.0.16 claim the 80017 tag, due to a bad merge.
  performance_schema tables changed in MySQL 8.0.16
  - replication_connection_configuration, added column NETWORK_NAMESPACE

  800171: -- WARNING, EXPOSED BY MySQL 8.0.17 --
    ---------------------------------------------------------------------
    IMPORTANT NOTE:
    The release 8.0.16 made use of version 80017 incorrectly, which makes
    the upgrade from 8.0.16 to 8.0.17 release immutable. This was
    introduced by WL#12720.

    In order to allow upgrade from 8.0.16 to 8.0.17, we choose the new
    version number as 800171 for 8.0.17 release. Going forward the
    release 8.0.18 would use 80018.

    Note that, any comparison between two PFS version numbers should be
    carefully done.
    ---------------------------------------------------------------------

  performance_schema tables changed in MySQL 8.0.17
  - WL#12571 increases the HOST name length from 60 to 255.

  80018:

  performance_schema tables changed in MySQL 8.0.18
  - replication_connection_configuration, added column
  MASTER_COMPRESSION_ALGORITHMS, MASTER_COMPRESSION_LEVEL
  - replication_applier_configuration, added column
  PRIVILEGE_CHECKS_USER

  80019:

  performance_schema tables changed in MySQL 8.0.19
  - replication_connection_configuration, added column
  TLS_CIPHERSUITES
  - replication_applier_configuration, added column
  REQUIRE_ROW_FORMAT

  80020:

  performance_schema tables changed in MySQL 8.0.20
  - WL#3549 created binary_log_transaction_compression_stats
  - replication_applier_configuration, added column
  REQUIRE_TABLE_PRIMARY_KEY_CHECK

  80021:

  performance_schema tables changed in MySQL 8.0.21
  - tls_channel_status (created)
  - replication_connection_configuration, added column
  SOURCE_CONNECTION_AUTO_FAILOVER

  80022:

  performance_schema tables changed in MySQL 8.0.22
  - WL#9090 created processlist
  - WL#13681 created error_log

  80023:

  performance_schema tables changed in MySQL 8.0.23
  - WL#12819 replication_applier_configuration, added column
  ASSIGN_GTIDS_TO_ANONYMOUS_TRANSACTIONS_TYPE
  ASSIGN_GTIDS_TO_ANONYMOUS_TRANSACTIONS_VALUE
  - performance_schema.replication_asynchronous_connection_failover,
  added column MANAGED_NAME
  - added table
  performance_schema.replication_asynchronous_connection_failover_managed

  80024:

  performance_schema tables changed in MySQL 8.0.24
  - WL#13446 added performance_schema.keyring_component_status

  80027:

  performance_schema tables changed in MySQL 8.0.27
  - WL#9852 added replication_group_members column
    MEMBER_COMMUNICATION_PROTOCOL_STACK
  - Bug#32701593:'SELECT FROM
    PS.REPLICATION_ASYNCHRONOUS_CONNECTION_FAILOVER' DIDN'T RETURN A RE
  - WL#7491 added the column to replication_connection_configuration
    the column GTID_ONLY
  - BUG#104643 Defaults in performance schema tables incompatible with sql_mode
    fixed TIMESTAMP columns (removed default 0)
    fixed DOUBLE columns (removed sign)

  80028:

  performance_schema tables changed in MySQL 8.0.28
  - WL#14779 PERFORMANCE_SCHEMA, ADD CPU TIME TO STATEMENT METRICS
    added CPU_TIME, SUM_CPU_TIME columns.
  - Fixed performance_schema.processlist host column to size 261.

  80029:

  performance_schema tables changed in MySQL 8.0.29
  - WL#14346 PERFORMANCE_SCHEMA, SECONDARY_ENGINE STATS
    Added columns EXECUTION_ENGINE, COUNT_SECONDARY
  - Bug #30624990 NO UTF8MB3 IN INFORMATION_SCHEMA.CHARACTER_SETS
    Use 'utf8mb3' rather than 'utf8' alias for for character set names.

  80030:

  performance_schema tables changed in MySQL 8.0.30
  - WL#12527 added innodb_redo_log_files table (table is created dynamically
    and based on PFS_engine_table_share_proxy mechanism)
  - Bug #33787300 Rename utf8_xxx collations to utf8mb3_xxx
    This patch renames utf8_bin to utf8mb3_bin
    This patch renames utf8_general_ci

  80031:

  - WL#14432 Session memory limits in performance schema
    Modified column PROPERTIES in table setup_instruments
    Added column FLAGS in table setup_instruments

  80032:

  - WL#15419: Make the replica_generate_invisible_primary_key option settable
    per channel
    Modified column REQUIRE_TABLE_PRIMARY_KEY_CHECK in table
    replication_applier_configuration

  80032-001:
  performance_schema tables changed:
  - session_query_attrs added.

  80032-002:
  performance_schema tables changed:
  - Schema of the following PFS tables are changed to have username of 80 chars
  length (Upstream has username of 32 chars length).
  table_accounts
  table_ees_by_account_by_error
  table_ees_by_user_by_error
  table_esgs_by_account_by_event_name
  table_esgs_by_user_by_event_name
  table_esms_by_account_by_event_name
  table_esms_by_user_by_event_name
  table_ets_by_account_by_event_name
  table_ets_by_user_by_event_name
  table_ews_by_account_by_event_name
  table_ews_by_user_by_event_name
  table_replication_connection_configuration
  table_setup_actors
  table_status_by_account
  table_status_by_user
  table_threads
  table_users
  table_variables_info

  80032-003:
  performance_schema tables changed:
  - table_statistics_per_table added.

  80032-004:
  performance_schema tables changed:
  - added table for queries_used and queries_empty

  80032-005:
  - add new columns below into table_statistics_per_table:
    IO_WRITE_BYTES
    IO_WRITE_REQUESTS
    IO_READ_BYTES
    IO_READ_REQUESTS

  80032-006:
  - add events_statements_summary_by_all table

  80032-007:
  - add temp table bytes written to statement statistics
  - add filesort bytes written to statement statistics
  - add index dive count to statement statistics
  - add index dive cpu time to statement statistics
  - add compilation cpu time to statement statistics
  - add elapsed time to statement statistics

  80032-008:
  - add replica_statistics table

  80032-009:
  - add client attributes

  80032-010:
  - add write_statistics
  - add write_throttling_rules
  - add write_throttling_log

  80032-011:
  - add sql_findings
  - change type of SQL_FINDINGS.LAST_RECORDED to bigint

  80032-012:
  - add THREAD_PRIORITY column to threads table

  80032-013:
  - add skipped count to statement statistics

  80032-014:
  - add COLUMN_STATISTICS table

  80032-015:
  - add SUM_FILESORT_DISK_USAGE to statements tables.
  - add SUM_TMP_TABLE_DISK_USAGE to statements tables.

  80032-016:
  - add throttle_rate column to write_throttling_rules table

  80032-017:
  - add INDEX_STATISTICS table

  80032-018:
  - support longer messages by changing SQL_FINDINGS.MESSAGE from 256 to 512

  The last three digits reprents Facebook specific MySQL Schema changes.
  Version published is now 80032-018. i.e. 8.0.32 Facebook schema change no. 18.
*/

static const uint PFS_DD_VERSION = 80032018;

#endif /* PFS_DD_VERSION_H */
