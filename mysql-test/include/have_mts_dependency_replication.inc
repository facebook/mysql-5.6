if (`SELECT @@GLOBAL.mts_dependency_replication = FALSE`)
{
     skip Test needs to run with dependency replication;
}
