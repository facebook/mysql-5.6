#!/usr/bin/env bash

set -e

MYSQL_51_VERSION=5.1.59
MYSQL_55_VERSION=5.5.17
MYSQL_56_VERSION=5.6.10
PS_51_VERSION=5.1.59-13.0
PS_55_VERSION=5.5.16-22.0

SRC_DIR="$(grep MySQL_SOURCE_DIR:STATIC= ../CMakeCache.txt | cut -f2 -d=)"
BUILD_TYPE="$(grep CMAKE_BUILD_TYPE:STRING= ../CMakeCache.txt | cut -f2 -d=)"

export CC="$CC"
export CXX="$CXX"
export CFLAGS="$CFLAGS -DXTRABACKUP"
export CXXFLAGS="$CFLAGS -fno-rtti -fno-exceptions -std=c++0x"
export LDFLAGS="$LDFLAGS"
export SRC_DIR="$SRC_DIR"
export BUILD_TYPE="$BUILD_TYPE"

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

    mkdir -p $server_dir
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
        configure_cmd="${CMAKE} $SRC_DIR \
                -DWITH_INNOBASE_STORAGE_ENGINE=ON \
                -DWITH_ZLIB=bundled \
                -DWITH_EXTRA_CHARSETS=all \
                -DWITH_EMBEDDED_SERVER=1 \
                -DCMAKE_BUILD_TYPE=$BUILD_TYPE"
        build_all $type
        ;;

*)
	usage
	;;
esac
