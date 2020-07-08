if (`SELECT @@GLOBAL.slave_parallel_type != 'DEPENDENCY'`)
{
  skip Test makes sense only to run with MTS slave-parallel-type=DEPENDENCY;
}
if (`SELECT @@GLOBAL.mts_dependency_replication = 'NONE'`)
{
  skip Test makes sense only to run with MTS mts-dependency-replication!=NONE;
}
