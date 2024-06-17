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

# loop through the seven streams.
for stream in 1 2 3 4 5 6 7
do

	# loop through the 22 queries.
	for q in 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18 19 20 21 22
	do
		echo "Processing stream $stream query $q" 
		jsdot=tpch100_s${stream}_${q}.hex_js.dot
		jsrdot=tpch100_s${stream}_${q}.hex_jsr.dot
		
		jspng=tpch100_s${stream}_${q}.js.png
		jsrpng=tpch100_s${stream}_${q}.jsr.png

		query=tpch100_s${stream}_${q}

  		if [ -f $jsdot ]; then
    			sed -e "1a\
			t [label=\"${query}\\\l${stack}\\\l${build}\" shape=plaintext]" $jsdot | dot -Tpng -o${jspng}
  		else
    			echo "$jsdot does not exist"
  		fi

  		if [ -f $jsrdot ]; then
    			sed "1a\
			t [label=\"${query}\\\l${stack}\\\l${build}\" shape=plaintext]" $jsrdot | dot -Tpng -o${jsrpng}
  		else
    			echo "$jsrdot does not exist"
  		fi
	done
done

