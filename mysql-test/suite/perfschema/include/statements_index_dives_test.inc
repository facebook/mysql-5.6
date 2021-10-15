#
--echo Index Dive Metrics: Setup
#

create table idx_dive_tbl(a1 int, a2 int, a3 int, a4 int, a5 int,
                  a6 int, a7 int, a8 int, a9 int, a10 int);

delimiter //;

create procedure populate()
begin
  declare i int default 0;
  while i < 100 do
    insert into idx_dive_tbl values(i, i+1, i+2, i+3, i+4,
                                    i, i+1, i+2, i+3, i+4);
    set i = i + 1;
  end while;
end //

delimiter ;//

call populate();

create index idx_dive_tbl_idx_a1 on idx_dive_tbl(a1);
create index idx_dive_tbl_idx_a2 on idx_dive_tbl(a2);
create index idx_dive_tbl_idx_a3 on idx_dive_tbl(a3);
create index idx_dive_tbl_idx_a4 on idx_dive_tbl(a4);
create index idx_dive_tbl_idx_a5 on idx_dive_tbl(a5);
create index idx_dive_tbl_idx_a6 on idx_dive_tbl(a6);
create index idx_dive_tbl_idx_a7 on idx_dive_tbl(a7);
create index idx_dive_tbl_idx_a8 on idx_dive_tbl(a8);
create index idx_dive_tbl_idx_a9 on idx_dive_tbl(a9);
create index idx_dive_tbl_idx_a10 on idx_dive_tbl(a10);

set optimizer_trace_max_mem_size=1048576; # 1MB

SET end_markers_in_json=off;

## The following test increase and decrease the value of
## eq_range_index_dive_limit and verify that the index dive
## statistics are tracked correctly

#####
--echo 'Test 1: Default value of eq_range_index_dive_limit (10)'
--echo '        Index dives on 9 indexes is expected'
#####
SET eq_range_index_dive_limit=10;

# Index dive queries are performed only on predicates using a1 to a9 but
# not on predicates using a10. Hence the index_dive_count is
# (1+2+3+4+5+6+7+8+9) = 45

--let $sql = select count(*) from idx_dive_tbl where idx_dive_tbl.a1  in (10) and idx_dive_tbl.a2  in (10, 20) and idx_dive_tbl.a3  in (10, 20, 30) and idx_dive_tbl.a4  in (10, 20, 30, 40) and idx_dive_tbl.a5  in (10, 20, 30, 40, 50) and idx_dive_tbl.a6  in (10, 20, 30, 40, 50, 60) and idx_dive_tbl.a7  in (10, 20, 30, 40, 50, 60, 70) and idx_dive_tbl.a8  in (10, 20, 30, 40, 50, 60, 70, 80) and idx_dive_tbl.a9  in (10, 20, 30, 40, 50, 60, 70, 80, 90) and idx_dive_tbl.a10 in (10, 20, 30, 40, 50, 60, 70, 80, 90, 100);
--let $expected_sum_index_dive_count = 45
--source ../include/statements_index_dives.inc

#####
--echo 'Test 2: Value of eq_range_index_dive_limit is 5'
--echo '        Index dives on 4 indexes is expected'
#####
SET eq_range_index_dive_limit=5;

# Index dive queries are performed only on predicates using a1 to a4 but
# not on predicates using a5 to a10. Hence the index_dive_count is
# (1+2+3+4) = 10

--let $sql = select count(*) from idx_dive_tbl where idx_dive_tbl.a1  in (10) and idx_dive_tbl.a2  in (10, 20) and idx_dive_tbl.a3  in (10, 20, 30) and idx_dive_tbl.a4  in (10, 20, 30, 40) and idx_dive_tbl.a5  in (10, 20, 30, 40, 50) and idx_dive_tbl.a6  in (10, 20, 30, 40, 50, 60) and idx_dive_tbl.a7  in (10, 20, 30, 40, 50, 60, 70) and idx_dive_tbl.a8  in (10, 20, 30, 40, 50, 60, 70, 80) and idx_dive_tbl.a9  in (10, 20, 30, 40, 50, 60, 70, 80, 90) and idx_dive_tbl.a10 in (10, 20, 30, 40, 50, 60, 70, 80, 90, 100);
--let $expected_sum_index_dive_count = 10
--source ../include/statements_index_dives.inc

#####
--echo 'Test 3: Value of eq_range_index_dive_limit is 11'
--echo '        Index dives on 10 indexes is expected'
#####
SET eq_range_index_dive_limit=11;

# Index dive queries are performed on all the predicates.
# Hence the index_dive_count is
# (1+2+3+4+5+6+7+8+9+10) = 55

--let $sql = select count(*) from idx_dive_tbl where idx_dive_tbl.a1  in (10) and idx_dive_tbl.a2  in (10, 20) and idx_dive_tbl.a3  in (10, 20, 30) and idx_dive_tbl.a4  in (10, 20, 30, 40) and idx_dive_tbl.a5  in (10, 20, 30, 40, 50) and idx_dive_tbl.a6  in (10, 20, 30, 40, 50, 60) and idx_dive_tbl.a7  in (10, 20, 30, 40, 50, 60, 70) and idx_dive_tbl.a8  in (10, 20, 30, 40, 50, 60, 70, 80) and idx_dive_tbl.a9  in (10, 20, 30, 40, 50, 60, 70, 80, 90) and idx_dive_tbl.a10 in (10, 20, 30, 40, 50, 60, 70, 80, 90, 100);
--let $expected_sum_index_dive_count = 55
--source ../include/statements_index_dives.inc

#####
--echo 'Test 4: Value of eq_range_index_dive_limit is 2'
--echo '        Index dives on 1 index is expected'
#####
SET eq_range_index_dive_limit=2;

# Index dive queries are performed only on predicates using a1 but
# not on predicates using a2 to a10. Hence the index_dive_count is 1

--let $sql = select count(*) from idx_dive_tbl where idx_dive_tbl.a1  in (10) and idx_dive_tbl.a2  in (10, 20) and idx_dive_tbl.a3  in (10, 20, 30) and idx_dive_tbl.a4  in (10, 20, 30, 40) and idx_dive_tbl.a5  in (10, 20, 30, 40, 50) and idx_dive_tbl.a6  in (10, 20, 30, 40, 50, 60) and idx_dive_tbl.a7  in (10, 20, 30, 40, 50, 60, 70) and idx_dive_tbl.a8  in (10, 20, 30, 40, 50, 60, 70, 80) and idx_dive_tbl.a9  in (10, 20, 30, 40, 50, 60, 70, 80, 90) and idx_dive_tbl.a10 in (10, 20, 30, 40, 50, 60, 70, 80, 90, 100);
--let $expected_sum_index_dive_count = 1
--source ../include/statements_index_dives.inc

#####
--echo 'Test 5: Value of eq_range_index_dive_limit is 7'
--echo '        Index dives on 6 indexes is expected'
#####
SET eq_range_index_dive_limit=7;

# Index dive queries are performed only on predicates using a1 to a6 but
# not on predicates using a7 to a10. Hence the index_dive_count is
# (1+2+3+4+5+6) = 21

--let $sql = select count(*) from idx_dive_tbl where idx_dive_tbl.a1  in (10) and idx_dive_tbl.a2  in (10, 20) and idx_dive_tbl.a3  in (10, 20, 30) and idx_dive_tbl.a4  in (10, 20, 30, 40) and idx_dive_tbl.a5  in (10, 20, 30, 40, 50) and idx_dive_tbl.a6  in (10, 20, 30, 40, 50, 60) and idx_dive_tbl.a7  in (10, 20, 30, 40, 50, 60, 70) and idx_dive_tbl.a8  in (10, 20, 30, 40, 50, 60, 70, 80) and idx_dive_tbl.a9  in (10, 20, 30, 40, 50, 60, 70, 80, 90) and idx_dive_tbl.a10 in (10, 20, 30, 40, 50, 60, 70, 80, 90, 100);
--let $expected_sum_index_dive_count = 21
--source ../include/statements_index_dives.inc

#####
--echo 'Test 6: Multi-statement query
--echo '        Index dives on 10 indexes is expected'
#####
SET eq_range_index_dive_limit=11;
--let $expected_sum_index_dive_count = 220

# Start counters for index dive counts
--let $start_sum_index_dive_count_user = query_get_value(select sum(SUM_INDEX_DIVE_COUNT) as Value from performance_schema.events_statements_summary_by_user_by_event_name, Value, 1)
--let $start_sum_index_dive_count_host = query_get_value(select sum(SUM_INDEX_DIVE_COUNT) as Value from performance_schema.events_statements_summary_by_host_by_event_name, Value, 1)
--let $start_sum_index_dive_count_program = query_get_value(select sum(SUM_INDEX_DIVE_COUNT) as Value from performance_schema.events_statements_summary_by_program, Value, 1)
--let $start_sum_index_dive_count_event = query_get_value(select sum(SUM_INDEX_DIVE_COUNT) as Value from performance_schema.events_statements_summary_by_thread_by_event_name, Value, 1)
--let $start_sum_index_dive_count_global = query_get_value(select sum(SUM_INDEX_DIVE_COUNT) as Value from performance_schema.events_statements_summary_global_by_event_name, Value, 1)

SET optimizer_trace="enabled=on,one_line=off";

delimiter ||||;

select count(*)
from idx_dive_tbl
where idx_dive_tbl.a1  in (10);

select count(*)
from idx_dive_tbl
where idx_dive_tbl.a1  in (10)
  and idx_dive_tbl.a2  in (10, 20);

select count(*)
from idx_dive_tbl
where idx_dive_tbl.a1  in (10)
  and idx_dive_tbl.a2  in (10, 20)
  and idx_dive_tbl.a3  in (10, 20, 30);

select count(*)
from idx_dive_tbl
where idx_dive_tbl.a1  in (10)
  and idx_dive_tbl.a2  in (10, 20)
  and idx_dive_tbl.a3  in (10, 20, 30)
  and idx_dive_tbl.a4  in (10, 20, 30, 40);

select count(*)
from idx_dive_tbl
where idx_dive_tbl.a1  in (10)
  and idx_dive_tbl.a2  in (10, 20)
  and idx_dive_tbl.a3  in (10, 20, 30)
  and idx_dive_tbl.a4  in (10, 20, 30, 40)
  and idx_dive_tbl.a5  in (10, 20, 30, 40, 50);

select count(*)
from idx_dive_tbl
where idx_dive_tbl.a1  in (10)
  and idx_dive_tbl.a2  in (10, 20)
  and idx_dive_tbl.a3  in (10, 20, 30)
  and idx_dive_tbl.a4  in (10, 20, 30, 40)
  and idx_dive_tbl.a5  in (10, 20, 30, 40, 50)
  and idx_dive_tbl.a6  in (10, 20, 30, 40, 50, 60);

select count(*)
from idx_dive_tbl
where idx_dive_tbl.a1  in (10)
  and idx_dive_tbl.a2  in (10, 20)
  and idx_dive_tbl.a3  in (10, 20, 30)
  and idx_dive_tbl.a4  in (10, 20, 30, 40)
  and idx_dive_tbl.a5  in (10, 20, 30, 40, 50)
  and idx_dive_tbl.a6  in (10, 20, 30, 40, 50, 60)
  and idx_dive_tbl.a7  in (10, 20, 30, 40, 50, 60, 70);

select count(*)
from idx_dive_tbl
where idx_dive_tbl.a1  in (10)
  and idx_dive_tbl.a2  in (10, 20)
  and idx_dive_tbl.a3  in (10, 20, 30)
  and idx_dive_tbl.a4  in (10, 20, 30, 40)
  and idx_dive_tbl.a5  in (10, 20, 30, 40, 50)
  and idx_dive_tbl.a6  in (10, 20, 30, 40, 50, 60)
  and idx_dive_tbl.a7  in (10, 20, 30, 40, 50, 60, 70)
  and idx_dive_tbl.a8  in (10, 20, 30, 40, 50, 60, 70, 80);

select count(*)
from idx_dive_tbl
where idx_dive_tbl.a1  in (10)
  and idx_dive_tbl.a2  in (10, 20)
  and idx_dive_tbl.a3  in (10, 20, 30)
  and idx_dive_tbl.a4  in (10, 20, 30, 40)
  and idx_dive_tbl.a5  in (10, 20, 30, 40, 50)
  and idx_dive_tbl.a6  in (10, 20, 30, 40, 50, 60)
  and idx_dive_tbl.a7  in (10, 20, 30, 40, 50, 60, 70)
  and idx_dive_tbl.a8  in (10, 20, 30, 40, 50, 60, 70, 80)
  and idx_dive_tbl.a9  in (10, 20, 30, 40, 50, 60, 70, 80, 90);

select count(*)
from idx_dive_tbl
where idx_dive_tbl.a1  in (10)
  and idx_dive_tbl.a2  in (10, 20)
  and idx_dive_tbl.a3  in (10, 20, 30)
  and idx_dive_tbl.a4  in (10, 20, 30, 40)
  and idx_dive_tbl.a5  in (10, 20, 30, 40, 50)
  and idx_dive_tbl.a6  in (10, 20, 30, 40, 50, 60)
  and idx_dive_tbl.a7  in (10, 20, 30, 40, 50, 60, 70)
  and idx_dive_tbl.a8  in (10, 20, 30, 40, 50, 60, 70, 80)
  and idx_dive_tbl.a9  in (10, 20, 30, 40, 50, 60, 70, 80, 90)
  and idx_dive_tbl.a10 in (10, 20, 30, 40, 50, 60, 70, 80, 90, 100);
||||

# reset delimiter
delimiter ;||||

select round(length(trace)-
             length(replace(trace, "\"index_dives_for_eq_ranges\": true", "")))
               /length("\"index_dives_for_eq_ranges\": true") Num_Indexes_Used_For_Dives
  from information_schema.optimizer_trace;

SET optimizer_trace="enabled=off";

# Get counters for index dive counts
--let $end_sum_index_dive_count_user = query_get_value(select sum(SUM_INDEX_DIVE_COUNT) as Value from performance_schema.events_statements_summary_by_user_by_event_name, Value, 1)
--let $end_sum_index_dive_count_host = query_get_value(select sum(SUM_INDEX_DIVE_COUNT) as Value from performance_schema.events_statements_summary_by_host_by_event_name, Value, 1)
--let $end_sum_index_dive_count_program = query_get_value(select sum(SUM_INDEX_DIVE_COUNT) as Value from performance_schema.events_statements_summary_by_program, Value, 1)
--let $end_sum_index_dive_count_event = query_get_value(select sum(SUM_INDEX_DIVE_COUNT) as Value from performance_schema.events_statements_summary_by_thread_by_event_name, Value, 1)
--let $end_sum_index_dive_count_global = query_get_value(select sum(SUM_INDEX_DIVE_COUNT) as Value from performance_schema.events_statements_summary_global_by_event_name, Value, 1)

# Check sanity for index dive count
--let $assert_text= SUM_INDEX_DIVE_COUNT on events_statements_summary_by_user_by_event_name should increase by $expected_sum_index_dive_count.
--let $assert_cond= $end_sum_index_dive_count_user - $start_sum_index_dive_count_user = $expected_sum_index_dive_count
--source include/assert.inc

--let $assert_text= SUM_INDEX_DIVE_COUNT on events_statements_summary_by_host_by_event_name should increase by $expected_sum_index_dive_count.
--let $assert_cond= $end_sum_index_dive_count_host - $start_sum_index_dive_count_host = $expected_sum_index_dive_count
--source include/assert.inc

--let $assert_text= SUM_INDEX_DIVE_COUNT on events_statements_summary_by_program should be increased by 0.
--let $assert_cond= $end_sum_index_dive_count_program - $start_sum_index_dive_count_program = 0
--source include/assert.inc

--let $assert_text= SUM_INDEX_DIVE_COUNT on events_statements_summary_by_thread_by_event_name should increase by $expected_sum_index_dive_count.
--let $assert_cond= $end_sum_index_dive_count_event - $start_sum_index_dive_count_event = $expected_sum_index_dive_count
--source include/assert.inc

--let $assert_text= SUM_INDEX_DIVE_COUNT on events_statements_summary_global_by_event_name should increase by $expected_sum_index_dive_count.
--let $assert_cond= $end_sum_index_dive_count_global - $start_sum_index_dive_count_global = $expected_sum_index_dive_count
--source include/assert.inc

## Clean up
drop table idx_dive_tbl;
drop procedure populate;
