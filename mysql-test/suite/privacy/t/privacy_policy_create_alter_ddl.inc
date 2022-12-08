if($allow_debug_commands) {
    SET SESSION debug= '+d,skip_dd_table_access_check';
}
SET @@session.show_create_table_contain_privacy_policy = TRUE;
--echo ########################################################################
--echo === Creating table with PRIVACY_POLICY!
--echo ########################################################################

let $privacy_policy= policy_original;
let $privacy_policy_2= policy_altered;
eval CREATE TABLE t1 (
    id int NOT NULL,
    name VARCHAR(30)   NOT NULL DEFAULT '',
    PRIMARY KEY (`id`)
) PRIVACY_POLICY='$privacy_policy';

if($allow_debug_commands) {
    SELECT name,options FROM mysql.tables WHERE `name`="t1";
}
SHOW CREATE TABLE t1;

--echo ########################################################################
--echo === Disable show_create_table_contain_privacy_policy
--echo ########################################################################

SET @@session.show_create_table_contain_privacy_policy = FALSE;
SHOW CREATE TABLE t1;

SET @@session.show_create_table_contain_privacy_policy = TRUE;
--echo ########################################################################
--echo === Altering table for PRIVACY_POLICY!
--echo ########################################################################

eval ALTER TABLE t1 PRIVACY_POLICY='$privacy_policy_2';

if($allow_debug_commands) {
    SELECT name,options FROM mysql.tables WHERE `name`="t1";
}

SHOW CREATE TABLE t1;

--echo ########################################################################
--echo === Usual Alter table on a policied table should retain privacy policy
--echo ########################################################################

ALTER TABLE t1 rename t2;
ALTER TABLE t2 ADD Column new_col int not null;
ALTER TABLE t2 ADD KEY (name(20));

if($allow_debug_commands) {
    SELECT name,options FROM mysql.tables WHERE `name`="t2";
}
SHOW CREATE TABLE t2;

ALTER TABLE t2 rename t1;

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
kscdsajhgfjdslhgfkdasjhfgkjasdhfgdskjhg0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdefghij';

if($allow_debug_commands) {
    SELECT name,options FROM mysql.tables WHERE `name`="longPolicy";
}

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
kscdsajhgfjdslhgfkdasjhfgkjasdhfgdskjhg0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdefghij';

if($allow_debug_commands) {
    SELECT name,options FROM mysql.tables WHERE `name`="longPolicy";
}

SHOW CREATE TABLE longPolicy;

--echo ########################################################################
--echo === Create table with empty privacy policy!
--echo ########################################################################

CREATE TABLE nopolicy (c1 int) PRIVACY_POLICY='';

if($allow_debug_commands) {
    SELECT name,options FROM mysql.tables WHERE `name`="nopolicy";
}

SHOW CREATE TABLE nopolicy;

--echo ########################################################################
--echo === Alter table to assign policy on a no policy table
--echo ########################################################################

eval ALTER TABLE nopolicy PRIVACY_POLICY='$privacy_policy';

if($allow_debug_commands) {
    SELECT name,options FROM mysql.tables WHERE `name`="nopolicy";
}

SHOW CREATE TABLE nopolicy;


--echo ########################################################################
--echo === Alter table to remove policy on a policied table
--echo ########################################################################

ALTER TABLE nopolicy PRIVACY_POLICY='';

if($allow_debug_commands) {
    SELECT name,options FROM mysql.tables WHERE `name`="nopolicy";
}

SHOW CREATE TABLE nopolicy;


# Cleanup
drop table t1;
drop table longPolicy;
drop table nopolicy;
if($allow_debug_commands) {
    SET SESSION debug= '-d,skip_dd_table_access_check';
}
