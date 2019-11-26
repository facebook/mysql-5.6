if (`SELECT @@GLOBAL.mts_dependency_replication = 'NONE'`)
{
     skip Test needs to run with dependency replication;
}
