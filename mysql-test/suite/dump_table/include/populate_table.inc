--disable_query_log
# Insert some dummy data.
insert into t1 (b,c,d,e) values (1,2,'aaa','bbb');

let $i = 0;
while ($i < 10)
{
    insert into t1 (b,c,d,e) select b,c,d,e from t1;
    inc $i;
}
--enable_query_log
