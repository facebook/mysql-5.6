if (`SELECT @@GLOBAL.slave_parallel_type != 'DEPENDENCY' OR @@GLOBAL.mts_dependency_replication != 'STMT'`) {
  skip Test needs to run with STMT dependency replication;
}
