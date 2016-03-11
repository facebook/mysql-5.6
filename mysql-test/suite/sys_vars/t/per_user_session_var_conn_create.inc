--echo ## create connection batch $i
let $j=1;
while ($j<=5) {
  connect (con_tao_$i$j,localhost,tao$j,,);
  inc $j;
}
connect (con_root_$i,localhost,root,,);
