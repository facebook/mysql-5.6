# Load some dummy data into t1.
# Option:
# $iterations - Number of times to run INSERT ... SELECT, which will have the
#  effect of doubling the table size. I.e. the table will have 2^$iterations
# rows. Defaults to 10.

--disable_query_log
# Insert some dummy data.
insert into t1 (b,c,d,e) values (1,2,'aaa','bbb');

if (!$iterations)
{
    let $iterations = 10;
}

let $i = 0;
while ($i < $iterations)
{
    insert into t1 (b,c,d,e) select b,c,d,e from t1;
    inc $i;
}
--enable_query_log
