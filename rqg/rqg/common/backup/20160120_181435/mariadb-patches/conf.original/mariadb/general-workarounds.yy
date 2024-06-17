# Redefine some elements of standard grammars used in general.cc 
# preventing the tests from running normally

# Safety check in replication grammars is for debug purposes

safety_check:
	;

# This is applied when we don't want to change binlog_format at runtime
# (specifically switch to STATEMENT binlog format as it might cause problems 
# due to unsafe replication)

binlog_format_statement:
	SET @binlog_format_saved = @@binlog_format ;

binlog_format_row:
	SET @binlog_format_saved = @@binlog_format ;

binlog_format_set:
	;

rand_global_binlog_format:
	;

rand_session_binlog_format:
	;
