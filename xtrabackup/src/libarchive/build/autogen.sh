#!/bin/sh

PATH=/usr/local/gnu-autotools/bin/:$PATH
export PATH

# Start from one level above the build directory
if [ -f version ]; then
    cd ..
fi

if [ \! -f build/version ]; then
    echo "Can't find source directory"
    exit 1
fi

# BSD make's "OBJDIR" support freaks out the automake-generated
# Makefile.  Effectively disable it.
export MAKEOBJDIRPREFIX=/junk

# Start from the build directory, where the version file is located
if [ -f build/version ]; then
    cd build
fi

if [ \! -f version ]; then
    echo "Can't find version file"
    exit 1
fi

# Update the build number in the 'version' file.
# Separate number from additional alpha/beta/etc marker
MARKER=`cat version | sed 's/[0-9.]//g'`
# Bump the number
VN=`cat version | sed 's/[^0-9.]//g'`
# Build out the string.
VS="$(($VN/1000000)).$(( ($VN/1000)%1000 )).$(( $VN%1000 ))$MARKER"

cd ..

# Clean up the source dir as much as we can.
/bin/sh build/clean.sh

# Substitute the integer version into Libarchive's archive.h
perl -p -i -e "s/^(#define\tARCHIVE_VERSION_NUMBER).*/\$1 $VN/" libarchive/archive.h
perl -p -i -e "s/^(#define\tARCHIVE_VERSION_STRING).*/\$1 \"libarchive $VS\"/" libarchive/archive.h
# Substitute versions into configure.ac as well
perl -p -i -e 's/(m4_define\(\[LIBARCHIVE_VERSION_S\]),.*\)/$1,['"$VS"'])/' configure.ac
perl -p -i -e 's/(m4_define\(\[LIBARCHIVE_VERSION_N\]),.*\)/$1,['"$VN"'])/' configure.ac

set -xe
aclocal -I build/autoconf

# Note: --automake flag needed only for libtoolize from
# libtool 1.5.x; in libtool 2.2.x it is a synonym for --quiet
case `uname` in
Darwin) glibtoolize --automake -c;;
*) libtoolize --automake -c;;
esac
autoconf
autoheader
automake -a -c

./configure
make distcheck
make dist-zip