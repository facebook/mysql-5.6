if (`SELECT @@GLOBAL.mts_dependency_replication <> 'STMT'`)
{
     skip Test needs to run with STMT dependency replication;
}
