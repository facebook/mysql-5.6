#
--echo Compilation CPU Metric: Setup
#

create table comp_cpu_tbl(a1 int, a2 int, a3 int, a4 int, a5 int,
                  a6 int, a7 int, a8 int, a9 int, a10 int);

delimiter //;

create procedure populate()
begin
  declare i int default 0;
  while i < 1000 do
    insert into comp_cpu_tbl values(i, i+1, i+2, i+3, i+4,
                                    i, i+1, i+2, i+3, i+4);
    set i = i + 1;
  end while;
end //

delimiter ;//

call populate();

#####
--echo Test 1: Single table select query
#####

--let $sql = select * from comp_cpu_tbl where a2 > 10 and a6 < 100;
--source ../include/statements_compilation_cpu.inc

#####
--echo Test 2: Select query with join
#####

--let $sql = select t1.*, t2.* from comp_cpu_tbl t1, comp_cpu_tbl t2 where t1.a1 = t2.a1 and t1.a2 > 10 and t2.a6 < 100;
--source ../include/statements_compilation_cpu.inc

#####
--echo Test 3: Select query with join and group by
#####

--let $sql = select t1.a3, t2.a4, sum(t1.a5), max(t2.a8) from comp_cpu_tbl t1, comp_cpu_tbl t2 where t1.a1 = t2.a1 and t1.a2 > 10 and t2.a6 < 100 group by t1.a3, t2.a4;
--source ../include/statements_compilation_cpu.inc

#####
--echo Test 4: Select query with join, group by and order by
#####

--let $sql = select t1.a3, t2.a4, sum(t1.a5), max(t2.a8) from comp_cpu_tbl t1, comp_cpu_tbl t2 where t1.a1 = t2.a1 and t1.a2 > 10 and t2.a6 < 100 group by t1.a3, t2.a4 order by 1, 2;
--source ../include/statements_compilation_cpu.inc

#####
--echo Test 5: Insert query with values
#####

--let $sql = insert into comp_cpu_tbl values(1, 1, 1, 1, 1, 1, 1, 1, 1, 1);
--source ../include/statements_compilation_cpu.inc

#####
--echo Test 6: Insert query with sub-query
#####

--let $sql = insert into comp_cpu_tbl select * from comp_cpu_tbl where a2>100 and a8 > 200;
--source ../include/statements_compilation_cpu.inc

#####
--echo Test 7: Update query
#####

--let $sql = update comp_cpu_tbl set a2 = a2+a4 where a2>100 and a8 > 200;
--source ../include/statements_compilation_cpu.inc

#####
--echo Test 8: Delete query
#####

--let $sql = delete from comp_cpu_tbl where a2>100 and a8 > 200;
--source ../include/statements_compilation_cpu.inc

## Clean up
drop table comp_cpu_tbl;
drop procedure populate;
