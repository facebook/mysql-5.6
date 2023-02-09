if (`SELECT @@GLOBAL.slave_parallel_type != 'DEPENDENCY' OR @@GLOBAL.mts_dependency_replication != 'TBL'`) {
  skip Test needs to run with TBL dependency replication;
}
