# show raft logs

# mask out the binlog position
-- replace_column 2 #
show raft logs;
