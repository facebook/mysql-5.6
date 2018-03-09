connect(conproxy,localhost,$mainuser,,);

# Attempt to create $count detached sessions
query_attrs_add rpc_role $targetuser;
query_attrs_add rpc_db $targetdb;

--disable_query_log
let $i = $count;

while ($i)
{
  dec $i;
  SET SESSION wait_timeout=1000;
}

--enable_query_log

disconnect conproxy;
connection default;
