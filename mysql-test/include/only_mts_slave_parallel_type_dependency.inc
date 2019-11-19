if (`SELECT @@GLOBAL.slave_parallel_type != 'DEPENDENCY'`)
{
  skip Test makes sense only to run with MTS slave-parallel-type=DEPENDENCY;
}
