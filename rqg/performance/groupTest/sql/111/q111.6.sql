select count(*), avg(ps_availqty), sum(ps_availqty), avg(ps_supplycost), sum(ps_supplycost) from partsupp 	where ps_suppkey <= 5000000;
