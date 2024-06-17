# no default value for build
if [ $# -lt 1 ]; then
  echo "usage: $0 build [stack]"
  exit
fi
build="BUILD $1"
 
# default stack is qperfd02
stack=QPERFD02
if [ $# -eq 2 ]; then
  build="BUILD $1"
  stack=`echo $2 | tr '[:lower:]' '[:upper:]'`
fi

# loop through the 22 queries
for q in q01-tpch14 q02-tpch02 q03-tpch09 q04-tpch20 q05-tpch06 q06-tpch17 q07-tpch18 q08-tpch08 q09-tpch21 q10-tpch13 q11-tpch03 q12-tpch22 q13-tpch16 q14-tpch04 q15-tpch11 q16-tpch15 q17-tpch01 q18-tpch10 q19-tpch19 q20-tpch05 q21-tpch07 q22-tpch12
do
  # convert the query to upper case
  query=`echo $q | tr '[:lower:]' '[:upper:]'`
 
  # tpch ID for matching the dot file
  qid=${q##*tpch}
 
  # the jobstep pair
  jsdot=tpch1t_s0_${qid}.hex_js.dot
  jspng=tpch1t_s0_${q}.png
 
  if [ -f $jsdot ]; then
    sed -e "1a\
t [label=\"${query}\\\l${stack}\\\l${build}\" shape=plaintext]" $jsdot | dot -Tpng -o${jspng}
  else
    echo "$jsdot does not exist"
  fi
 
  # the jobstep_result pair
  jsrdot=tpch1t_s0_${qid}.hex_jsr.dot
  jsrpng=tpch1t_s0_${q}_results.png
  if [ -f $jsrdot ]; then
    sed "1a\
t [label=\"${query}\\\l${stack}\\\l${build}\" shape=plaintext]" $jsrdot | dot -Tpng -o${jsrpng}
  else
    echo "$jsrdot does not exist"
  fi
done
