#!/bin/bash
MKFILE=`mktemp`
# create and run a simple makefile
# include rocksdb make file relative to the path of this script
echo "include rocksdb/src.mk
FOLLY_DIR = ./third-party/folly
all:" > $MKFILE

if [ -z $1 ]; then
  echo "	@echo \"\$(LIB_SOURCES)\"" >> $MKFILE
else
  echo "	@echo \"\$(LIB_SOURCES)\" \"\$(FOLLY_SOURCES)\"" >> $MKFILE
fi
for f in `make --makefile $MKFILE`
do
  echo ../../rocksdb/$f
done
rm $MKFILE

# create build_version.cc file. Only create one if it doesn't exists or if it is different
# this is so that we don't rebuild mysqld every time
bv=rocksdb/util/build_version.cc
build_date=$(date +%F)
pushd rocksdb>/dev/null
git_sha=$(git rev-parse  HEAD 2>/dev/null)
git_tag=$(git symbolic-ref -q --short HEAD || \
  git describe --tags --exact-match 2>/dev/null)
git_mod=$(git diff-index HEAD --quiet 2>/dev/null; echo $?)
git_date=$(git log -1 --date=format:"%Y-%m-%d %T" --format="%ad" 2>/dev/null)
popd>/dev/null
if [ ! -f $bv ] || [ -z $git_sha ] || [ ! `grep -q $git_sha $bv` ]
then
sed -e s/@GIT_SHA@/$git_sha/ -e s:@GIT_TAG@:"$git_tag":  \
    -e s/@GIT_MOD@/"$git_mod"/ -e s/@BUILD_DATE@/"$build_date"/  \
    -e s/@GIT_DATE@/"$git_date"/ \
    -e s/@ROCKSDB_PLUGIN_BUILTINS@/"$(ROCKSDB_PLUGIN_BUILTINS)"/ \
    -e s/@ROCKSDB_PLUGIN_EXTERNS@/"$(ROCKSDB_PLUGIN_EXTERNS)"/ \
    rocksdb/util/build_version.cc.in > $bv
fi
