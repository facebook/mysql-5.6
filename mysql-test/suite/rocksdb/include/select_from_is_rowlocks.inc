--echo # select * from information_schema.rocksdb_locks; # With replacements by select_from_is_rowlocks.inc
#
#  An include to print contents of I_S.ROCKSB_LOCKS
#
#   Implicit "parameters"
#     - Currently it prints locks on t1.PRIMARY
#
#   Explicit "parameter" variables:
#     - $TRX1_ID  - print this transaction as "TRX1"
#     - $TRX2_ID  - print this transaction as "TRX2"
# 
#     - $select_from_is_rowlocks_current_trx_only
#     - $order_by_rowkey

--disable_query_log
set @cf_id=(select column_family from information_schema.rocksdb_ddl
            where table_name='t1' and index_name='PRIMARY');
set @rtrx_id=(select transaction_id from information_schema.rocksdb_trx
              where thread_id=connection_id());
set @indexnr= (select lower(lpad(hex(index_number),8,'0')) from information_schema.rocksdb_ddl
               where table_name='t1' and index_name='PRIMARY');

set @indexnr_next= (select lower(lpad(hex(index_number+1),8,'0'))
                    from information_schema.rocksdb_ddl
                    where table_name='t1' and index_name='PRIMARY');

let $extra_where = where 1;

if ($select_from_is_rowlocks_current_trx_only) 
{
  let $extra_where = where transaction_id=(select transaction_id from information_schema.rocksdb_trx where connection_id()=thread_id);
}

# If TRX1_ID is not specified, get the current transaction:
let $transaction_col= replace(transaction_id, @rtrx_id, "\$trx_id");
if ($TRX1_ID)
{
  let $transaction_col = replace(transaction_id, '$TRX1_ID', "\$TRX1_ID");
}

if ($TRX2_ID)
{
  let $transaction_col = replace($transaction_col, '$TRX2_ID', "\$TRX2_ID");
}

if ($order_by_rowkey)
{
  let $extra_order_by = ORDER BY 3,2;
}

if (!$order_by_rowkey)
{
  --sorted_result
}

eval select 
  replace(column_family_id, @cf_id, "\$cf_id") as COLUMN_FAMILY_ID,
  $transaction_col as TRANSACTION_ID,
  replace(
     replace(`key`, @indexnr, '\${indexnr}'),
     @indexnr_next, '\${indexnr+1}'
  ) as `KEY`,
  mode
from information_schema.rocksdb_locks $extra_where $extra_order_by;

--enable_query_log
