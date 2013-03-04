#!/bin/sh
#
# Execute this tool to setup the environment and build binary releases
# for xtrabackup starting from a fresh tree.
#
# Usage: build-binary.sh [target dir]
# The default target directory is the current directory. If it is not
# supplied and the current directory is not empty, it will issue an error in
# order to avoid polluting the current directory after a test run.
#

# Bail out on errors, be strict
set -ue

# Examine parameters
TARGET="$(uname -m)"
TARGET_CFLAGS=''

# Some programs that may be overriden
TAR=${TAR:-tar}

# Check if we have a functional getopt(1)
if ! getopt --test
then
    go_out="$(getopt --options="i" --longoptions=i686 \
        --name="$(basename "$0")" -- "$@")"
    test $? -eq 0 || exit 1
    eval set -- $go_out
fi

for arg
do
    case "$arg" in
    -- ) shift; break;;
    -i | --i686 )
        shift
        TARGET="i686"
        TARGET_CFLAGS="-m32 -march=i686"
        ;;
    esac
done

# Working directory
if test "$#" -eq 0
then
    WORKDIR="$(pwd)"
    
    # Check that the current directory is not empty
    if test "x$(echo *)" != "x*"
    then
        echo >&2 \
            "Current directory is not empty. Use $0 . to force build in ."
        exit 1
    fi

    WORKDIR_ABS="$(cd "$WORKDIR"; pwd)"

elif test "$#" -eq 1
then
    WORKDIR="$1"

    # Check that the provided directory exists and is a directory
    if ! test -d "$WORKDIR"
    then
        echo >&2 "$WORKDIR is not a directory"
        exit 1
    fi

    WORKDIR_ABS="$(cd "$WORKDIR"; pwd)"

else
    echo >&2 "Usage: $0 [target dir]"
    exit 1

fi

SOURCEDIR="$(cd $(dirname "$0"); cd ..; pwd)"
test -e "$SOURCEDIR/VERSION" || exit 2

# Read XTRABACKUP_VERSION from the VERSION file
. $SOURCEDIR/VERSION

# Build information
REVISION="$(cd "$SOURCEDIR"; bzr revno)"

# Compilation flags
export CC=${CC:-gcc}
export CXX=${CXX:-gcc}
export CFLAGS="-fPIC -Wall -O3 -g -static-libgcc -fno-omit-frame-pointer ${CFLAGS:-}"
export CXXFLAGS="-O2 -fno-omit-frame-pointer -g -pipe -Wall -Wp,-D_FORTIFY_SOURCE=2 -fno-exceptions ${CXXFLAGS:-}"
export MAKE_JFLAG=-j4

# Create a temporary working directory
BASEINSTALLDIR="$(cd "$WORKDIR" && TMPDIR="$WORKDIR_ABS" mktemp -d xtrabackup-build.XXXXXX)"
INSTALLDIR="$WORKDIR_ABS/$BASEINSTALLDIR/percona-xtrabackup-$XTRABACKUP_VERSION"   # Make it absolute

mkdir "$INSTALLDIR"

export AUTO_DOWNLOAD=yes

# Build
(
    cd "$WORKDIR"

    # Make a copy of the source
    rm -f "percona-xtrabackup-$XTRABACKUP_VERSION"

    bzr export "percona-xtrabackup-$XTRABACKUP_VERSION" "$SOURCEDIR"

    # Build proper
    (
        cd "percona-xtrabackup-$XTRABACKUP_VERSION"

        # Install the files
        mkdir "$INSTALLDIR/bin" "$INSTALLDIR/share"
        mkdir -p "$INSTALLDIR/share/doc/percona-xtrabackup"

        bash utils/build.sh xtradb55
        install -m 755 src/xtrabackup_55 "$INSTALLDIR/bin"

        bash utils/build.sh xtradb
        install -m 755 src/xtrabackup "$INSTALLDIR/bin"

        bash utils/build.sh 5.1
        install -m 755 src/xtrabackup_51 "$INSTALLDIR/bin"

        install -m 755 src/xbstream "$INSTALLDIR/bin"

        install -m 755 innobackupex "$INSTALLDIR/bin"
        ln -s innobackupex "$INSTALLDIR/bin/innobackupex-1.5.1"

        install -m 644 COPYING "$INSTALLDIR/share/doc/percona-xtrabackup"

        cp -R test "$INSTALLDIR/share/percona-xtrabackup-test"

    ) || false

    $TAR czf "percona-xtrabackup-$XTRABACKUP_VERSION-$REVISION.tar.gz" \
        --owner=0 --group=0 -C "$INSTALLDIR/../" \
        "percona-xtrabackup-$XTRABACKUP_VERSION"

    # Clean up build dir
    rm -rf "percona-xtrabackup-$XTRABACKUP_VERSION"
    
) || false

# Clean up
rm -rf "$WORKDIR_ABS/$BASEINSTALLDIR"

