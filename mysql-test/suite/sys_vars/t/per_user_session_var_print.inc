--echo ##
--echo ## session variables of the current connnection batch
--echo ## which should be determined by the latest values of the SET command
--echo ##
let $k=$i;
--source per_user_session_var_print_var.inc

if ($i>1) {
  --echo ##
  --echo ## session variables of the last connnection batch
  --echo ## which should remain the values of the previous SET command
  --echo ##
  let $k=$i;
  dec $k;
  --source per_user_session_var_print_var.inc
}
