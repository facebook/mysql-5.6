if (`SELECT @@GLOBAL.replica_parallel_type != 'DEPENDENCY'`)
{
  skip Test makes sense only to run with MTS replica_parallel_type=DEPENDENCY;
}
if (`SELECT @@GLOBAL.mts_dependency_replication = 'NONE'`)
{
  skip Test makes sense only to run with MTS mts-dependency-replication!=NONE;
}
