#!/bin/bash
MKFILE=`mktemp`
# create and run a simple makefile
# include rocksdb make file relative to the path of this script
echo "include rocksdb/src.mk
all:
	@echo \$(LIB_SOURCES)" > $MKFILE
for f in `make --makefile $MKFILE`
do
  echo ../../rocksdb/$f
done
rm $MKFILE

# create build_version.cc file. Only create one if it doesn't exists or if it is different
# this is so that we don't rebuild mysqld every time
bv=rocksdb/util/build_version.cc
date=$(date +%F)
git_sha=$(pushd rocksdb >/dev/null && git rev-parse  HEAD 2>/dev/null && popd >/dev/null)
git_mod=$(pushd rocksdb >/dev/null && git diff-index HEAD --quiet && echo $? && popd >/dev/null)

if [ ! -f $bv ] || [ -z $git_sha ] || [ ! `grep $git_sha $bv` ]
then
sed -e "s/@GIT_SHA@/$git_sha/" \
    -e "s/@GIT_DATE@/$date/" \
    -e "s/@GIT_MOD@/$git_mod/" < $bv.in > $bv
fi
