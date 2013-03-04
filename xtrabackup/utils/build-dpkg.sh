#!/bin/sh
# Usage: build-dpkg.sh [target dir]
# The default target directory is the current directory. If it is not
# supplied and the current directory is not empty, it will issue an error in
# order to avoid polluting the current directory after a test run.
#
# The program will setup the dpkg building environment and ultimately call
# dpkg-buildpackage with the appropiate parameters.
#

# Bail out on errors, be strict
set -ue

# Examine parameters
go_out="$(getopt --options "k:KbBSnT"\
    --longoptions key:,nosign,binary,binarydep,source,dummy,notransitional \
    --name "$(basename "$0")" -- "$@")"
test $? -eq 0 || exit 1
eval set -- $go_out

BUILDPKG_KEY=''
DPKG_BINSRC=''
DUMMY=''
NOTRANSITIONAL=''

for arg
do
    case "$arg" in
    -- ) shift; break;;
    -k | --key ) shift; BUILDPKG_KEY="-pgpg -k$1"; shift;;
    -K | --nosign ) shift; BUILDPKG_KEY="-uc -us";;
    -b | --binary ) shift; DPKG_BINSRC='-b';;
    -B | --binarydep ) shift; DPKG_BINSRC='-B';;
    -S | --source ) shift; DPKG_BINSRC='-S';;
    -n | --dummy ) shift; DUMMY='yes';;
    -T | --notransitional ) shift; NOTRANSITIONAL='yes';;
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

elif test "$#" -eq 1
then
    WORKDIR="$1"

    # Check that the provided directory exists and is a directory
    if ! test -d "$WORKDIR"
    then
        echo >&2 "$WORKDIR is not a directory"
        exit 1
    fi

else
    echo >&2 "Usage: $0 [target dir]"
    exit 1

fi

SOURCEDIR="$(cd $(dirname "$0"); cd ..; pwd)"

# Read XTRABACKUP_VERSION from the VERSION file
. $SOURCEDIR/VERSION

DEBIAN_VERSION="$(lsb_release -sc)"
REVISION="$(cd "$SOURCEDIR"; bzr log -r-1 | grep ^revno: | cut -d ' ' -f 2)"
FULL_VERSION="$XTRABACKUP_VERSION-$REVISION.$DEBIAN_VERSION"

# Build information
export CC=${CC:-gcc}
export CXX=${CXX:-gcc}
export CFLAGS="-fPIC -Wall -O3 -g -static-libgcc -fno-omit-frame-pointer"
export CXXFLAGS="-O2 -fno-omit-frame-pointer -g -pipe -Wall -Wp,-D_FORTIFY_SOURCE=2 -fno-exceptions"
export MAKE_JFLAG=-j4

export DEB_BUILD_OPTIONS='debug nocheck'
export DEB_CFLAGS_APPEND="$CFLAGS"
export DEB_CXXFLAGS_APPEND="$CXXFLAGS"

# Build
(
    # Make a copy in workdir and copy debian files
    cd "$WORKDIR"
    bzr export "percona-xtrabackup-$FULL_VERSION" "$SOURCEDIR"

    (
        cd "percona-xtrabackup-$FULL_VERSION"

        # Move the debian dir to the appropriate place
        cp -a "$SOURCEDIR/utils/debian/" .

        # Don't build transitional packages if requested
        if test "x$NOTRANSITIONAL" = "xyes"
        then
            sed -i '/Package: xtrabackup/,/^$/d' debian/control
        fi

        # Apply dummy patch if wanted
        if test "x$DUMMY" = "xyes"
        then
            patch -p1 < 'utils/debian-dummy-rules.patch'
        fi

        # Update distribution
        dch -m -D "$DEBIAN_VERSION" --force-distribution -v "$XTRABACKUP_VERSION-$REVISION.$DEBIAN_VERSION" 'Update distribution'
        # Issue dpkg-buildpackage command
        dpkg-buildpackage $DPKG_BINSRC $BUILDPKG_KEY
 
    )

    rm -rf "percona-xtrabackup-$FULL_VERSION"

)
