MKFILE=`mktemp`
# create and run a simple makefile
# include rocksdb make file relative to the path of this script
echo "include $(dirname $(readlink -f $0))/../../rocksdb/src.mk
all:
	@echo \$(LIB_SOURCES)" > $MKFILE
for f in `make --makefile $MKFILE`
do
  echo ../../rocksdb/$f
done
rm $MKFILE
