if (`SELECT @@GLOBAL.slave_use_idempotent_for_recovery = 'YES'`)
{
     skip Test cannot run with idempotent recovery;
}
