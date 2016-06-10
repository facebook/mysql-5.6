set optimizer_switch='index_merge_sort_union=off';
eval create table t (a int, b int, c int, d int, e int, primary key(a, b, c, d), key(b, d)) engine=$engine;

--disable_query_log
create temporary table a (a int);
let $i=10;
while ($i)
{
  --eval insert into a values ($i)
  dec $i;
}

create temporary table b (a int);
let $i=5;
while ($i)
{
  --eval insert into b values ($i)
  dec $i;
}

create temporary table c (a int);
let $i=5;
while ($i)
{
  --eval insert into c values ($i)
  dec $i;
}

create temporary table d (a int);
let $i=10;
while ($i)
{
  --eval insert into d values ($i)
  dec $i;
}

insert into t select a.a, b.a, c.a, d.a, d.a from a, b, c, d;

drop temporary table a, b, c, d;

--enable_query_log

analyze table t;
show indexes from t;

# These queries should do loose index scans.
--let $query = select b, d from t where d < 2
--source include/loose_index_scans_compare.inc

--let $query = select b, d from t where d > 4
--source include/loose_index_scans_compare.inc

--let $query = select a, b, c, d from t where a = 5 and d <= 3
--source include/loose_index_scans_compare.inc

--let $query = select e from t where a = 5 and d <= 3
--source include/loose_index_scans_compare.inc

--let $query = select a, b, c, d from t where a = 5 and d >= 98
--source include/loose_index_scans_compare.inc

--let $query = select e from t where a = 5 and d >= 98
--source include/loose_index_scans_compare.inc

--let $query = select a, b, c, d from t where a in (1, 5) and d >= 98
--source include/loose_index_scans_compare.inc

--let $query = select a, b, c, d from t where a in (1, 3, 5) and d >= 98
--source include/loose_index_scans_compare.inc

--let $query = select a, b, c, d from t where a in (1, 5) and b in (1, 2) and d >= 98
--source include/loose_index_scans_compare.inc

--let $query = select a, b, c, d from t where a in (1, 2, 3, 4, 5) and b in (1, 2, 3) and d >= 98
--source include/loose_index_scans_compare.inc

--let $query = select a, b, c, d from t where a = 5 and b = 2 and d >= 98
--source include/loose_index_scans_compare.inc

--let $query = select a+1, b, c, d from t where a = 5 and d < 3
--source include/loose_index_scans_compare.inc

--let $query = select b, c, d from t where a = 5 and d < 3
--source include/loose_index_scans_compare.inc

--let $query = select a, b, c, d from t where a = b and d >= 98
--source include/loose_index_scans_compare.inc

set optimizer_switch = 'skip_scan=on';
# These queries should not do loose index scans.
set optimizer_trace = 'enabled=on';
--replace_column 9 #
explain select a, b, c, d from t where a = 5 and d < 3 order by b, c, d;
select count(*) from information_schema.optimizer_trace where trace like '%order_attribute_not_prefix_in_index%';

--replace_column 9 #
explain select a, b, c, d from t where a = 2 and d >= 98 and e = 2;
select count(*) from information_schema.optimizer_trace where trace like '%query_references_nonkey_column%';

--replace_column 9 #
explain select a, b, c, d from t where a = 5 or b = 2 and d >= 98;
select count(*) from information_schema.optimizer_trace where trace like '%no_range_tree%';

--replace_column 9 #
explain select a, b, c, d from t where a = 5 or b = 2 or d >= 98;
select count(*) from information_schema.optimizer_trace where trace like '%no_range_tree%';

--replace_column 9 #
explain select a, b, c, d from t where a = 5 or d >= 98;
select count(*) from information_schema.optimizer_trace where trace like '%no_range_tree%';

--replace_column 9 #
explain select a, b, c, d from t where ((a = 5 and b = 2) or a = 2) and d >= 98;
select count(*) from information_schema.optimizer_trace where trace like '%keypart_in_disjunctive_query%';

--replace_column 9 #
explain select a, b, c, d from t where a > 2 and d >= 98;
select count(*) from information_schema.optimizer_trace where trace like '%prefix_not_const_equality%';

--replace_column 9 #
explain select a, b, c, d from t where a = 2 and (d >= 98 or d < 2);
select count(*) from information_schema.optimizer_trace where trace like '%range_predicate_too_complex%';

--replace_column 9 #
explain select a, b, c, d from t where a = 2 and b = 2;
select count(*) from information_schema.optimizer_trace where trace like '%no_range_predicate%';

--replace_column 9 #
explain select a, b, c, d from t where a = 2 and c > 2 and d < 2;
select count(*) from information_schema.optimizer_trace where trace like '%too_many_range_predicates%';

--replace_column 9 #
explain select a, b, c, d from t where (a < 1 or a = 4 or a = 5) and b in (1, 2, 3) and d >= 98;
select count(*) from information_schema.optimizer_trace where trace like '%prefix_not_const_equality%';

set optimizer_trace = 'enabled=off';
set optimizer_switch= 'skip_scan=off';

drop table t;
set optimizer_switch='index_merge_sort_union=on';
