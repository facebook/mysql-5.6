let $select_errors_net_total = select errors_net_total from information_schema.user_statistics where user_name="$user";
let $select_errors_net_ER_NET_ERROR_ON_WRITE = select
errors_net_ER_NET_ERROR_ON_WRITE from information_schema.user_statistics where
user_name="$user";
let $select_errors_net_ER_NET_PACKETS_OUT_OF_ORDER = select errors_net_ER_NET_PACKETS_OUT_OF_ORDER from information_schema.user_statistics where user_name="$user";
let $select_errors_net_ER_NET_PACKET_TOO_LARGE = select errors_net_ER_NET_PACKET_TOO_LARGE from information_schema.user_statistics where user_name="$user";
let $select_errors_net_ER_NET_READ_ERROR = select errors_net_ER_NET_READ_ERROR from information_schema.user_statistics where user_name="$user";
let $select_errors_net_ER_NET_READ_INTERRUPTED = select errors_net_ER_NET_READ_INTERRUPTED from information_schema.user_statistics where user_name="$user";
let $select_errors_net_ER_NET_UNCOMPRESS_ERROR = select errors_net_ER_NET_UNCOMPRESS_ERROR from information_schema.user_statistics where user_name="$user";
let $select_errors_net_ER_NET_WRITE_INTERRUPTED = select errors_net_ER_NET_WRITE_INTERRUPTED from information_schema.user_statistics where user_name="$user";

if ($mode == 'baseline')
{
  --let $base_errors_net_total = query_get_value($select_errors_net_total, errors_net_total, 1)
  --let $base_errors_net_ER_NET_ERROR_ON_WRITE = query_get_value($select_errors_net_ER_NET_ERROR_ON_WRITE, errors_net_ER_NET_ERROR_ON_WRITE, 1)
  --let $base_errors_net_ER_NET_PACKETS_OUT_OF_ORDER = query_get_value($select_errors_net_ER_NET_PACKETS_OUT_OF_ORDER, errors_net_ER_NET_PACKETS_OUT_OF_ORDER, 1)
  --let $base_errors_net_ER_NET_PACKET_TOO_LARGE = query_get_value($select_errors_net_ER_NET_PACKET_TOO_LARGE, errors_net_ER_NET_PACKET_TOO_LARGE, 1)
  --let $base_errors_net_ER_NET_READ_ERROR = query_get_value($select_errors_net_ER_NET_READ_ERROR, errors_net_ER_NET_READ_ERROR, 1)
  --let $base_errors_net_ER_NET_READ_INTERRUPTED = query_get_value($select_errors_net_ER_NET_READ_INTERRUPTED, errors_net_ER_NET_READ_INTERRUPTED, 1)
  --let $base_errors_net_ER_NET_UNCOMPRESS_ERROR = query_get_value($select_errors_net_ER_NET_UNCOMPRESS_ERROR, errors_net_ER_NET_UNCOMPRESS_ERROR, 1)
  --let $base_errors_net_ER_NET_WRITE_INTERRUPTED = query_get_value($select_errors_net_ER_NET_WRITE_INTERRUPTED, errors_net_ER_NET_WRITE_INTERRUPTED, 1)
}

if ($mode == 'compute')
{
  --let $errors_net_total = query_get_value($select_errors_net_total, errors_net_total, 1)
  --let $errors_net_ER_NET_ERROR_ON_WRITE = query_get_value($select_errors_net_ER_NET_ERROR_ON_WRITE, errors_net_ER_NET_ERROR_ON_WRITE, 1)
  --let $errors_net_ER_NET_PACKETS_OUT_OF_ORDER = query_get_value($select_errors_net_ER_NET_PACKETS_OUT_OF_ORDER, errors_net_ER_NET_PACKETS_OUT_OF_ORDER, 1)
  --let $errors_net_ER_NET_PACKET_TOO_LARGE = query_get_value($select_errors_net_ER_NET_PACKET_TOO_LARGE, errors_net_ER_NET_PACKET_TOO_LARGE, 1)
  --let $errors_net_ER_NET_READ_ERROR = query_get_value($select_errors_net_ER_NET_READ_ERROR, errors_net_ER_NET_READ_ERROR, 1)
  --let $errors_net_ER_NET_READ_INTERRUPTED = query_get_value($select_errors_net_ER_NET_READ_INTERRUPTED, errors_net_ER_NET_READ_INTERRUPTED, 1)
  --let $errors_net_ER_NET_UNCOMPRESS_ERROR = query_get_value($select_errors_net_ER_NET_UNCOMPRESS_ERROR, errors_net_ER_NET_UNCOMPRESS_ERROR, 1)
  --let $errors_net_ER_NET_WRITE_INTERRUPTED = query_get_value($select_errors_net_ER_NET_WRITE_INTERRUPTED, errors_net_ER_NET_WRITE_INTERRUPTED, 1)

  --disable_query_log
  eval select $errors_net_total - $base_errors_net_total into @errors_net_total;
  eval select $errors_net_ER_NET_ERROR_ON_WRITE - $base_errors_net_ER_NET_ERROR_ON_WRITE into @errors_net_ER_NET_ERROR_ON_WRITE;
  eval select $errors_net_ER_NET_PACKETS_OUT_OF_ORDER - $base_errors_net_ER_NET_PACKETS_OUT_OF_ORDER into @errors_net_ER_NET_PACKETS_OUT_OF_ORDER;
  eval select $errors_net_ER_NET_PACKET_TOO_LARGE - $base_errors_net_ER_NET_PACKET_TOO_LARGE into @errors_net_ER_NET_PACKET_TOO_LARGE;
  eval select $errors_net_ER_NET_READ_ERROR - $base_errors_net_ER_NET_READ_ERROR into @errors_net_ER_NET_READ_ERROR;
  eval select $errors_net_ER_NET_READ_INTERRUPTED - $base_errors_net_ER_NET_READ_INTERRUPTED into @errors_net_ER_NET_READ_INTERRUPTED;
  eval select $errors_net_ER_NET_UNCOMPRESS_ERROR - $base_errors_net_ER_NET_UNCOMPRESS_ERROR into @errors_net_ER_NET_UNCOMPRESS_ERROR;
  eval select $errors_net_ER_NET_WRITE_INTERRUPTED - $base_errors_net_ER_NET_WRITE_INTERRUPTED into @errors_net_ER_NET_WRITE_INTERRUPTED;

  --enable_query_log
  select @errors_net_total;
  select @errors_net_ER_NET_ERROR_ON_WRITE;
  select @errors_net_ER_NET_PACKETS_OUT_OF_ORDER;
  select @errors_net_ER_NET_PACKET_TOO_LARGE;
  select @errors_net_ER_NET_READ_ERROR;
  select @errors_net_ER_NET_READ_INTERRUPTED;
  select @errors_net_ER_NET_UNCOMPRESS_ERROR;
  select @errors_net_ER_NET_WRITE_INTERRUPTED;
}
