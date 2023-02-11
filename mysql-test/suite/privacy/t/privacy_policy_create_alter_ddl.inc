if($allow_debug_commands) {
    SET SESSION debug= '+d,skip_dd_table_access_check';
}
SET @@session.show_create_table_contain_privacy_policy = TRUE;
set @original_rocksdb_disable_instant_ddl=@@global.rocksdb_disable_instant_ddl;
set global rocksdb_disable_instant_ddl = false;
--echo ########################################################################
--echo === Creating table with PRIVACY_POLICY!
--echo ########################################################################

let $privacy_policy_1= policy_original;
let $privacy_policy_2= policy_altered;
let $privacy_policy_3= policy_altered_again;

# Also setting another option to make sure that it is persisted correctly while modifying
# one of the options. E.g  STATS_AUTO_RECALC

eval CREATE TABLE t1_inno (
    id int NOT NULL,
    name VARCHAR(30)   NOT NULL DEFAULT '',
    PRIMARY KEY (`id`)
) ENGINE=InnoDB PRIVACY_POLICY='$privacy_policy_1'  STATS_AUTO_RECALC=1;

eval CREATE TABLE t1_rocks (
    id int NOT NULL,
    name VARCHAR(30)   NOT NULL DEFAULT '',
    PRIMARY KEY (`id`)
) ENGINE=ROCKSDB PRIVACY_POLICY='$privacy_policy_1'  STATS_AUTO_RECALC=1;

if($allow_debug_commands) {
    SELECT name,options FROM mysql.tables WHERE `name` like "t1%";
}

SELECT TABLE_NAME, CREATE_OPTIONS FROM INFORMATION_SCHEMA.TABLES WHERE TABLE_NAME like 't1%';
SHOW CREATE TABLE t1_inno;
SHOW CREATE TABLE t1_rocks;

--echo ########################################################################
--echo === Disable show_create_table_contain_privacy_policy
--echo ########################################################################

SET @@session.show_create_table_contain_privacy_policy = FALSE;
SHOW CREATE TABLE t1_inno;
SHOW CREATE TABLE t1_rocks;

SET @@session.show_create_table_contain_privacy_policy = TRUE;
--echo ########################################################################
--echo === Altering table for PRIVACY_POLICY!
--echo ########################################################################

eval ALTER TABLE t1_inno PRIVACY_POLICY='$privacy_policy_2';
eval ALTER TABLE t1_rocks PRIVACY_POLICY='$privacy_policy_2';

if($allow_debug_commands) {
    SELECT name,options FROM mysql.tables WHERE `name`="t1";
}

SELECT TABLE_NAME, CREATE_OPTIONS FROM INFORMATION_SCHEMA.TABLES WHERE TABLE_NAME like 't1%';
SHOW CREATE TABLE t1_inno;
SHOW CREATE TABLE t1_rocks;

--echo ########################################################################
--echo === INPLACE Altering table for PRIVACY_POLICY!
--echo ########################################################################
eval ALTER TABLE t1_inno PRIVACY_POLICY='$privacy_policy_3', ALGORITHM=INPLACE;
SHOW CREATE TABLE t1_inno;

eval ALTER TABLE t1_rocks PRIVACY_POLICY='$privacy_policy_3', ALGORITHM=INPLACE;
SHOW CREATE TABLE t1_rocks;

if($allow_debug_commands) {
    SELECT name,options FROM mysql.tables WHERE `name` like "t1%";
}


--echo ########################################################################
--echo === INSTANT Altering table for PRIVACY_POLICY!
--echo ########################################################################
eval ALTER TABLE t1_inno PRIVACY_POLICY='$privacy_policy_1', ALGORITHM=INSTANT;
ALTER TABLE t1_inno ADD COLUMN c1 INT NOT NULL, ALGORITHM=INSTANT;
SHOW CREATE TABLE t1_inno;
eval ALTER TABLE t1_inno PRIVACY_POLICY='$privacy_policy_2', ALGORITHM=INSTANT;
SHOW CREATE TABLE t1_inno;

eval ALTER TABLE t1_rocks PRIVACY_POLICY='$privacy_policy_1', ALGORITHM=INSTANT;
ALTER TABLE t1_rocks ADD COLUMN c1 INT NOT NULL, ALGORITHM=INSTANT;
SHOW CREATE TABLE t1_rocks;
eval ALTER TABLE t1_rocks PRIVACY_POLICY='$privacy_policy_2', ALGORITHM=INSTANT;
SHOW CREATE TABLE t1_rocks;

if($allow_debug_commands) {
    SELECT name,options FROM mysql.tables WHERE `name` like "t1%";
}

# Altering both privacy policy and other instant operation not allowed at the same time
--error ER_ALTER_OPERATION_NOT_SUPPORTED
eval ALTER TABLE t1_inno ADD COLUMN c2 INT NOT NULL, PRIVACY_POLICY='$privacy_policy_3', ALGORITHM=INSTANT;

--error ER_ALTER_OPERATION_NOT_SUPPORTED
eval ALTER TABLE t1_rocks ADD COLUMN c2 INT NOT NULL, PRIVACY_POLICY='$privacy_policy_3', ALGORITHM=INSTANT;

--echo ########################################################################
--echo === Usual Alter table on a policied table should retain privacy policy
--echo ########################################################################

ALTER TABLE t1_inno rename t2_inno;
ALTER TABLE t2_inno ADD Column new_col int not null;
ALTER TABLE t2_inno ADD KEY (name(20));
SHOW CREATE TABLE t2_inno;

ALTER TABLE t1_rocks rename t2_rocks;
ALTER TABLE t2_rocks ADD Column new_col int not null;
ALTER TABLE t2_rocks ADD KEY (name(20));
SHOW CREATE TABLE t2_rocks;

if($allow_debug_commands) {
    SELECT name,options FROM mysql.tables WHERE `name`="t2%";
}

SELECT TABLE_NAME, CREATE_OPTIONS FROM INFORMATION_SCHEMA.TABLES WHERE TABLE_NAME like 't2%';

ALTER TABLE t2_inno rename t1_inno;
ALTER TABLE t2_rocks rename t1_rocks;

--echo ########################################################################
--echo === Create table for Super Long policy!
--echo ########################################################################

CREATE TABLE longPolicy (c1 int) PRIVACY_POLICY='0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789a
bcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456
789abcdef0123456789abcdef0123456789abcdef0123456789abcdefasdajhdgjasgdasjhdgasjhgdasjkdgkjahsgdfkjhgsdfkjdhsgfjk agjkdhsgfas
kjdhsagfshdagfkjhdsgf jksadgf kjdsahgf kjdsahgf kjadhsgfouaywegfkausbfkaj dhsg fjkgsfkjgdsafkjhdsgfjkhdsagfdsjkahfgdaskjhgfs
dasjkhgfdskjagfdksajhgfdskajhfgdsakjfhgsadjlhgfadsljfhgadslj gadsjklgfdsajkgfdsakjgfdaskjfgdsajkhfgdsaljhfgadsougfudsayfjkhd
avfjhdsagfkjhasdgfjkhsgfkjhadsgfsdjahkgfjagjdhsvfhvjkhvcdskjhvkcjhvkjdshvckjadsvckjsdhvckjhdasvckjhsdavcgjkhadsvchgdsavchgkd
vckasdgcvkshdgacvdsakgvcasdkgchjvdsakcgvadskgvckhgadsvchksadgvckjavdsckgadsvckhgadsvckjgavsdckjagvsckjgadsvcjkadgsvcjkasgdvc
vckasdgcvkshdgacvdsakgvcasdkgchjvdsakcgvadskgvckhgadsvchksadgvckjavdsckgadsvckhgadsvckjgavsdckjagvsckjgadsvcjkadgsvcjkasgdvc
kscdsajhgfjdslhgfkdasjhfgkjasdhfgdskjhg0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdefghij';

if($allow_debug_commands) {
    SELECT name,options FROM mysql.tables WHERE `name`="longPolicy";
}

SELECT TABLE_NAME, CREATE_OPTIONS FROM INFORMATION_SCHEMA.TABLES WHERE TABLE_NAME='longPolicy';
SHOW CREATE TABLE longPolicy;

--echo ########################################################################
--echo === Alter table for Super Long policy!
--echo ########################################################################

ALTER TABLE longPolicy PRIVACY_POLICY='NEW_CHANGE_0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789a
bcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456
789abcdef0123456789abcdef0123456789abcdef0123456789abcdefasdajhdgjasgdasjhdgasjhgdasjkdgkjahsgdfkjhgsdfkjdhsgfjk agjkdhsgfas
kjdhsagfshdagfkjhdsgf jksadgf kjdsahgf kjdsahgf kjadhsgfouaywegfkausbfkaj dhsg fjkgsfkjgdsafkjhdsgfjkhdsagfdsjkahfgdaskjhgfs
dasjkhgfdskjagfdksajhgfdskajhfgdsakjfhgsadjlhgfadsljfhgadslj gadsjklgfdsajkgfdsakjgfdaskjfgdsajkhfgdsaljhfgadsougfudsayfjkhd
avfjhdsagfkjhasdgfjkhsgfkjhadsgfsdjahkgfjagjdhsvfhvjkhvcdskjhvkcjhvkjdshvckjadsvckjsdhvckjhdasvckjhsdavcgjkhadsvchgdsavchgkd
vckasdgcvkshdgacvdsakgvcasdkgchjvdsakcgvadskgvckhgadsvchksadgvckjavdsckgadsvckhgadsvckjgavsdckjagvsckjgadsvcjkadgsvcjkasgdvc
vckasdgcvkshdgacvdsakgvcasdkgchjvdsakcgvadskgvckhgadsvchksadgvckjavdsckgadsvckhgadsvckjgavsdckjagvsckjgadsvcjkadgsvcjkasgdvc
kscdsajhgfjdslhgfkdasjhfgkjasdhfgdskjhg0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdefghij';

if($allow_debug_commands) {
    SELECT name,options FROM mysql.tables WHERE `name`="longPolicy";
}

SELECT TABLE_NAME, CREATE_OPTIONS FROM INFORMATION_SCHEMA.TABLES WHERE TABLE_NAME='longPolicy';
SHOW CREATE TABLE longPolicy;

--echo ########################################################################
--echo === Create table with empty privacy policy!
--echo ########################################################################

CREATE TABLE nopolicy (c1 int) PRIVACY_POLICY='';

if($allow_debug_commands) {
    SELECT name,options FROM mysql.tables WHERE `name`="nopolicy";
}

SELECT TABLE_NAME, CREATE_OPTIONS FROM INFORMATION_SCHEMA.TABLES WHERE TABLE_NAME='nopolicy';
SHOW CREATE TABLE nopolicy;

--echo ########################################################################
--echo === Alter table to assign policy on a no policy table
--echo ########################################################################

eval ALTER TABLE nopolicy PRIVACY_POLICY='$privacy_policy_1';

if($allow_debug_commands) {
    SELECT name,options FROM mysql.tables WHERE `name`="nopolicy";
}

SELECT TABLE_NAME, CREATE_OPTIONS FROM INFORMATION_SCHEMA.TABLES WHERE TABLE_NAME='nopolicy';
SHOW CREATE TABLE nopolicy;


--echo ########################################################################
--echo === Alter table to remove policy on a policied table
--echo ########################################################################

ALTER TABLE nopolicy PRIVACY_POLICY='';

if($allow_debug_commands) {
    SELECT name,options FROM mysql.tables WHERE `name`="nopolicy";
}

SELECT TABLE_NAME, CREATE_OPTIONS FROM INFORMATION_SCHEMA.TABLES WHERE TABLE_NAME='nopolicy';
SHOW CREATE TABLE nopolicy;


# Cleanup
drop table t1_inno;
drop table t1_rocks;
drop table longPolicy;
drop table nopolicy;
if($allow_debug_commands) {
    SET SESSION debug= '-d,skip_dd_table_access_check';
}
set global rocksdb_disable_instant_ddl = @original_rocksdb_disable_instant_ddl;
