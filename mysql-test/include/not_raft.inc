if (`SELECT @@GLOBAL.enable_raft_plugin = 1`)
{
     skip Test cannot run in raft mode;
}
