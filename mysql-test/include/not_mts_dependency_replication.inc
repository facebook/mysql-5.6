if (`SELECT @@GLOBAL.mts_dependency_replication = TRUE`)
{
     skip Test cannot run with dependency replication;
}
