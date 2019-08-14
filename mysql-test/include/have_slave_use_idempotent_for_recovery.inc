if (`SELECT @@global.slave_use_idempotent_for_recovery <> 'YES'`)
{
  --skip Test requires slave_use_idempotent_for_recovery = YES
}
