if (`SELECT @@GLOBAL.mts_dependency_replication != 'NONE'`)
{
     skip Test cannot run with dependency replication;
}
