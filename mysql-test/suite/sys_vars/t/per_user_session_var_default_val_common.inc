inc $i;
--echo ## ($i) Empty value
SET GLOBAL per_user_session_var_default_val = "";
SELECT @@GLOBAL.per_user_session_var_default_val;
--source per_user_session_var_conn_create.inc

inc $i;
--echo ## ($i) Restored initial non-empty value
eval SET @@global.per_user_session_var_default_val = "$original_val";
SELECT @@global.per_user_session_var_default_val;
--source per_user_session_var_conn_create.inc

--echo ##
--echo ## Session variables of all the connections
--echo ##

let $k=1;
while ($k<=$i) {
  --echo ##
  --echo ## ($k) Connection batch $k
  --echo ##

  let $j=1;
  while ($j<=5) {
    --echo ##
    --echo ## Connection con_tao_$k$j for user tao$j
    --echo ##
    connection con_tao_$k$j;
    --source per_user_session_var_print.inc
    disconnect con_tao_$k$j;

    --echo ##
    --echo ## SSL connection con_tao_ssl_$k$j for user tao$j
    --echo ##
    connection con_tao_ssl_$k$j;
    --source per_user_session_var_print.inc
    disconnect con_tao_ssl_$k$j;

    inc $j;
  }
  --echo ##
  --echo ## Connection con_tao_slash_$k for user tao/slash
  --echo ##
  connection con_tao_slash_$k;
  --source per_user_session_var_print.inc
  disconnect con_tao_slash_$k;

  --echo ##
  --echo ## Connection con_root_$k for user root
  --echo ##
  connection con_root_$k;
  --source per_user_session_var_print.inc
  disconnect con_root_$k;

  --echo ##
  --echo ## SSL connection con_root_ssl_$k for user root
  --echo ##
  connection con_root_ssl_$k;
  --source per_user_session_var_print.inc
  disconnect con_root_ssl_$k;


  inc $k;
}
