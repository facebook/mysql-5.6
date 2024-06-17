select count(*), avg(ps_availqty), sum(ps_availqty), avg(ps_supplycost), sum(ps_supplycost) from partsupp 	where ps_supplycost <= 501;
