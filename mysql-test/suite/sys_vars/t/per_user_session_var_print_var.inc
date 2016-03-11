--echo ## connection batch $k
let $j=1;
while ($j<=5) {
  --echo ## connection con$k$j for user tao$j
  connection con_tao_$k$j;
  --source per_user_session_var_print_var_do.inc
  inc $j;
}
--echo ## connection con$k_root for user root
connection con_root_$k;
--source per_user_session_var_print_var_do.inc
