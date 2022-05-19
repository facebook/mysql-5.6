--echo ## create connection batch $i
let $j=1;
while ($j<=5) {
  connect (con_tao_$i$j,localhost,$tao$j,,);
  connect (con_tao_ssl_$i$j,localhost,$tao$j,,,,,SSL);
  inc $j;
}
connect (con_root_$i,localhost,root,,);
connect (con_root_ssl_$i,localhost,root,,,,,SSL);
connect (con_tao_slash_$i,localhost,'tao/slash',,,,,SSL);
