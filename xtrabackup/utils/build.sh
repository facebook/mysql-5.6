#!/usr/bin/env bash

set -e

TOOLCHAIN_REV=1b76b1fdef117650883ae1568fcf2777ad9a00c1
TOOLCHAIN_EXECUTABLES="/mnt/gvfs/third-party/$TOOLCHAIN_REV/centos5.2-native"
TOOLCHAIN_LIB_BASE="/mnt/gvfs/third-party/$TOOLCHAIN_REV/gcc-4.6.2-glibc-2.13"
export CC="$TOOLCHAIN_EXECUTABLES/gcc/gcc-4.6.2-glibc-2.13/bin/gcc"
export CXX="$TOOLCHAIN_EXECUTABLES/gcc/gcc-4.6.2-glibc-2.13/bin/g++"
CMAKE="$TOOLCHAIN_EXECUTABLES/cmake/cmake-2.8.4/da39a3e/bin/cmake"

MYSQL_51_VERSION=5.1.59
MYSQL_55_VERSION=5.5.17
MYSQL_56_VERSION=5.6.10
PS_51_VERSION=5.1.59-13.0
PS_55_VERSION=5.5.16-22.0

MASTER_SITE="http://s3.amazonaws.com/percona.com/downloads/community"

optflags="-O2 -g -pipe -Wall -Wp,-D_FORTIFY_SOURCE=2 -fexceptions -fstack-protector --param=ssp-buffer-size=4 -m64 -mtune=generic"

CFLAGS=
CXXFLAGS=

CFLAGS="$optflags -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE"
CFLAGS+=" -DNO_ALARM -DSIGNAL_WITH_VIO_CLOSE"
CFLAGS+=" -fno-strict-aliasing -fwrapv -fno-omit-frame-pointer -momit-leaf-frame-pointer"
CFLAGS+=" -fPIC $GOLD_FLAG"
CFLAGS+=" -I $TOOLCHAIN_LIB_BASE/ncurses/ncurses-5.8/4bc2c16/include"
CFLAGS+=" -I $TOOLCHAIN_LIB_BASE/libaio/libaio-0.3.109/4bc2c16/include"
CFLAGS+=" -I $TOOLCHAIN_LIB_BASE/jemalloc/jemalloc-2.2.5/96de4f9/include -DHAVE_JEMALLOC"
CFLAGS+=" -I $TOOLCHAIN_LIB_BASE/zlib/zlib-1.2.5/4bc2c16/include"
CFLAGS+=" -I $TOOLCHAIN_LIB_BASE/bzip2/bzip2-1.0.6/4bc2c16/include"
CFLAGS+=" -I $TOOLCHAIN_LIB_BASE/xz/xz-5.0.0/4bc2c16/include"

export CFLAGS="$CFLAGS -DXTRABACKUP"
export CXXFLAGS="$CFLAGS -fno-rtti -fno-exceptions -std=c++0x"

MAKE_CMD=make
if gmake --version > /dev/null 2>&1
then
	MAKE_CMD=gmake
fi
MAKE_CMD="$MAKE_CMD -j6"

function usage()
{
    echo "Build an xtrabackup binary against the specified InnoDB flavor."
    echo
    echo "Usage: `basename $0` CODEBASE"
    echo "where CODEBASE can be one of the following values or aliases:"
    echo "  innodb56         | 5.6                   build against InnoDB in MySQL 5.6"
    exit -1
}

################################################################################
# Unpack the tarball specified as the first argument and apply the patch
# specified as the second argument to the resulting directory.
################################################################################
function unpack_and_patch()
{
    rm -fr mysql-5.6.10 mysql-5.6
    dir=$(mktemp -d)
    cp -r ../* $dir
    cp -r $dir mysql-5.6.10
    rm -fr $dir
#    cd `basename "$1" ".tar.gz"`
#    patch -p1 < $top_dir/patches/$2
#    cd ..
}

################################################################################
# Invoke 'make' in the specified directoies
################################################################################
function make_dirs()
{
    for d in $*
    do
	$MAKE_CMD -C $d
    done
}

function build_server()
{
    local $type=$1
    echo "Configuring the server"
    cd $server_dir
    eval $configure_cmd

    echo "Building the server"
    if [ "$type" = "innodb56" ]
    then
        make_dirs libmysqld
    else
        make_dirs include zlib strings mysys dbug extra
        make_dirs $innodb_dir
    fi
    cd $top_dir
}

function build_libarchive()
{
	echo "Building libarchive"
	cd $top_dir/src/libarchive
	
	${CMAKE}  . \
	    -DENABLE_CPIO=OFF \
	    -DENABLE_OPENSSL=OFF \
	    -DENABLE_TAR=OFF \
	    -DENABLE_TEST=OFF
	$MAKE_CMD || exit -1
}

function build_xtrabackup()
{
    build_libarchive
    echo "Building XtraBackup"

    # Read XTRABACKUP_VERSION from the VERSION file
    . $top_dir/VERSION

    cd $top_dir/src
    if [ "`uname -s`" = "Linux" ]
    then
	export LIBS="$LIBS -lrt"
    fi
    $MAKE_CMD MYSQL_ROOT_DIR=$server_dir clean
    $MAKE_CMD MYSQL_ROOT_DIR=$server_dir XTRABACKUP_VERSION=$XTRABACKUP_VERSION $xtrabackup_target
    cd $top_dir
}


################################################################################
# Do all steps to build the server, xtrabackup and xbstream
# Expects the following variables to be set before calling:
#   mysql_version	version string (e.g. "5.1.53")
#   server_patch	name of the patch to apply to server source before
#                       building (e.g. "xtradb51.patch")
#   innodb_name		either "innobase" or "innodb_plugin"
#   configure_cmd	server configure command
#   xtrabackup_target	'make' target to build in the xtrabackup build directory
#
################################################################################
function build_all()
{
    local type=$1
    local mysql_version_short=${mysql_version:0:3}
    server_dir=$top_dir/mysql-$mysql_version_short
    server_tarball=mysql-$mysql_version.tar.gz
    innodb_dir=$server_dir/storage/$innodb_name

    test -d $server_dir && rm -r $server_dir

    mkdir $server_dir
    build_server $type

    build_xtrabackup
}

if ! test -f src/xtrabackup.cc
then
	echo "`basename $0` must be run from the directory with XtraBackup sources"
	usage
fi

type=$1
top_dir=`pwd`


case "$type" in
"innodb56" | "5.6")
        mysql_version=$MYSQL_56_VERSION
        server_patch=innodb56.patch
        innodb_name=innobase
        xtrabackup_target=5.6
        configure_cmd="${CMAKE} . \
                -DWITH_INNOBASE_STORAGE_ENGINE=ON \
                -DWITH_ZLIB=bundled \
                -DWITH_EXTRA_CHARSETS=all \
                -DWITH_EMBEDDED_SERVER=1 $extra_config_55plus \
                ../.."
        build_all $type
        ;;

*)
	usage
	;;
esac
