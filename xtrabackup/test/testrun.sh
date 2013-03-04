#!/bin/bash

export DEBUG=

. inc/common.sh

trap kill_leftovers 1 2 3 15

set +e

result=0

function usage()
{
cat <<EOF
Usage: $0 [-f] [-g] [-h] [-s suite] [-t test_name] [-d mysql_basedir] [-c build_conf]
-f          Continue running tests after failures
-d path     Server installation directory. Default is './server'.
-g          Debug mode
-t path     Run only a single named test
-h          Print this help message
-s suite    Select a test suite to run. Possible values: experimental, t. 
            Default is 't'.
-c conf     XtraBackup build configuration as specified to build.sh.
            Default is 'autodetect', i.e. xtrabackup binary is determined based
            on the MySQL version.
EOF
}

function find_program()
{
    local VARNAME="$1"
    shift
    local PROGNAME="$1"
    shift
    local DIRLIST="$*"
    local found=""

    for dir in in $DIRLIST
    do
	if [ -d "$dir" -a -x "$dir/$PROGNAME" ]
	then
	    eval "$VARNAME=\"$dir/$PROGNAME\""
	    found="yes"
	    break
	fi
    done
    if [ -z "$found" ]
    then
	echo "Can't find $PROGNAME in $DIRLIST"
	exit -1
    fi
}

function set_vars()
{
    TEST_BASEDIR=${TEST_BASEDIR:-"$PWD"}
    MYSQL_BASEDIR=${MYSQL_BASEDIR:-"$PWD/server"}
    PORT_BASE=$((3306 + $RANDOM))

    if gnutar --version > /dev/null 2>&1
    then
	TAR=gnutar
    else
	TAR=tar
    fi

    find_program MYSQL_INSTALL_DB mysql_install_db $MYSQL_BASEDIR/bin \
	$MYSQL_BASEDIR/scripts
    find_program MYSQLD mysqld $MYSQL_BASEDIR/bin/ $MYSQL_BASEDIR/libexec
    find_program MYSQL mysql $MYSQL_BASEDIR/bin
    find_program MYSQLADMIN mysqladmin $MYSQL_BASEDIR/bin

    # Check if we are running from a source tree and, if so, set PATH 
    # appropriately
    if test "`basename $PWD`" = "test"
    then
	PATH="$PWD/..:$PWD/../src:$PATH"
    fi

    PATH="${MYSQL_BASEDIR}/bin:$PATH"

    if [ -z "${LD_LIBRARY_PATH:-}" ]; then
	LD_LIBRARY_PATH=$MYSQL_BASEDIR/lib/mysql
    else
	LD_LIBRARY_PATH=$MYSQL_BASEDIR/lib/mysql:$LD_LIBRARY_PATH
    fi

    export TEST_BASEDIR PORT_BASE TAR MYSQL_BASEDIR MYSQL MYSQLD MYSQLADMIN \
MYSQL_INSTALL_DB PATH LD_LIBRARY_PATH
}

function get_version_info()
{
    MYSQLD_EXTRA_ARGS=

    XB_BIN=""
    IB_ARGS="--user=root --ibbackup=$XB_BIN"
    XB_ARGS="--no-defaults"

    if [ "$XB_BUILD" != "autodetect" ]
    then
	case "$XB_BUILD" in
	    "innodb50" )
		XB_BIN="xtrabackup_51";;
	    "innodb51_builtin" )
		XB_BIN="xtrabackup_51";;
	    "innodb51" )
		XB_BIN="xtrabackup_plugin"
		MYSQLD_EXTRA_ARGS="--ignore-builtin-innodb --plugin-load=innodb=ha_innodb_plugin.so";;
	    "innodb55" )
		XB_BIN="xtrabackup_innodb55";;
            "innodb56" )
                XB_BIN="xtrabackup_innodb56" ;;
	    "xtradb51" | "mariadb51" | "mariadb52" | "mariadb53")
		XB_BIN="xtrabackup";;
	    "xtradb55" | "mariadb55")
		XB_BIN="xtrabackup_55";;
	    "galera55" )
		XB_BIN="xtrabackup_55";;
	esac
	if [ -z "$XB_BIN" ]
	then
	    vlog "Unknown configuration: '$XB_BUILD'"
	    exit -1
	fi
    fi

    MYSQL_VERSION=""
    INNODB_VERSION=""

    start_server >>$OUTFILE 2>&1

    # Get MySQL and InnoDB versions
    MYSQL_VERSION=`$MYSQL ${MYSQL_ARGS} -Nsf -e "SHOW VARIABLES LIKE 'version'"`
    MYSQL_VERSION=${MYSQL_VERSION#"version	"}
    MYSQL_VERSION_COMMENT=`$MYSQL ${MYSQL_ARGS} -Nsf -e "SHOW VARIABLES LIKE 'version_comment'"`
    MYSQL_VERSION_COMMENT=${MYSQL_VERSION_COMMENT#"version_comment	"}
    INNODB_VERSION=`$MYSQL ${MYSQL_ARGS} -Nsf -e "SHOW VARIABLES LIKE 'innodb_version'"`
    INNODB_VERSION=${INNODB_VERSION#"innodb_version	"}
    XTRADB_VERSION="`echo $INNODB_VERSION  | sed 's/[0-9]\.[0-9]\.[0-9][0-9]*\(-[0-9][0-9]*\.[0-9][0-9]*\)*$/\1/'`"

    # Version-specific defaults
    DEFAULT_IBDATA_SIZE="10M"

    # Determine MySQL flavor
    if [[ "$MYSQL_VERSION" =~ "MariaDB" ]]
    then
	MYSQL_FLAVOR="MariaDB"
    elif [ -n "$XTRADB_VERSION" ]
    then
	MYSQL_FLAVOR="Percona Server"
    else
	MYSQL_FLAVOR="MySQL"
    fi

    # Determine InnoDB flavor
    if [ -n "$XTRADB_VERSION" ]
    then
	INNODB_FLAVOR="XtraDB"
    else
	INNODB_FLAVOR="innoDB"
    fi

    if [ "$XB_BUILD" = "autodetect" ]
    then
        # Determine xtrabackup build automatically
	if [ "${MYSQL_VERSION:0:3}" = "5.0" ]
	then
	    XB_BIN="xtrabackup_51"
	elif [ "${MYSQL_VERSION:0:3}" = "5.1" ]
	then
	    if [ -z "$INNODB_VERSION" ]
	    then
		XB_BIN="xtrabackup_51" # InnoDB 5.1 builtin
	    else
		XB_BIN="xtrabackup"    # InnoDB 5.1 plugin or Percona Server 5.1
	    fi
	elif [ "${MYSQL_VERSION:0:3}" = "5.2" -o
	       "${MYSQL_VERSION:0:3}" = "5.3"]
	then
	    XB_BIN="xtrabackup"
	elif [ "${MYSQL_VERSION:0:3}" = "5.5" ]
	then
	    if [ -n "$XTRADB_VERSION" ]
	    then
		XB_BIN="xtrabackup_55"
	    else
		XB_BIN="xtrabackup_innodb55"
	    fi
        elif [ "${MYSQL_VERSION:0:3}" = "5.6" ]
        then
            XB_BIN="xtrabackup_innodb56"
            DEFAULT_IBDATA_SIZE="12M"
	else
	    vlog "Unknown MySQL/InnoDB version: $MYSQL_VERSION/$INNODB_VERSION"
	    exit -1
	fi
    fi

    XB_PATH="`which $XB_BIN`"
    if [ -z "$XB_PATH" ]
    then
	vlog "Cannot find '$XB_BIN' in PATH"
	return 1
    fi
    XB_BIN="$XB_PATH"

    # Set the correct binary for innobackupex
    IB_BIN="`which innobackupex`"
    if [ -z "$IB_BIN" ]
    then
	vlog "Cannot find 'innobackupex' in PATH"
	return 1
    fi

    stop_server

    export MYSQL_VERSION MYSQL_VERSION_COMMENT MYSQL_FLAVOR \
	INNODB_VERSION XTRADB_VERSION INNODB_FLAVOR \
	XB_BIN IB_BIN IB_ARGS XB_ARGS MYSQLD_EXTRA_ARGS \
        DEFAULT_IBDATA_SIZE
}

export SKIPPED_EXIT_CODE=200

tname=""
XTRACE_OPTION=""
XB_BUILD="autodetect"
force=""
KEEP_RESULTS=0
SUBUNIT_OUT=test_results.subunit

while getopts "fgh?:t:s:d:c:b:nr:" options; do
	case $options in
	        f ) force="yes";;
		t ) tname="$OPTARG";;
		g ) XTRACE_OPTION="-x"; DEBUG=on;;
		h ) usage; exit;;
		s ) tname="$OPTARG/*.sh";;
	        d ) export MYSQL_BASEDIR="$OPTARG";;
	        b ) TEST_BASEDIR="$OPTARG";;
	        c ) XB_BUILD="$OPTARG";;
	        n ) KEEP_RESULTS=1;;
	        r ) SUBUNIT_OUT="$OPTARG";;
		? ) echo "Use \`$0 -h' for the list of available options."
                    exit -1;;
	esac
done

if [ $KEEP_RESULTS -eq 0 ];
then
    rm -rf results
    rm -f $SUBUNIT_OUT
fi
mkdir results

set_vars

if [ -n "$tname" ]
then
   tests="$tname"
else
   tests="t/*.sh"
fi

failed_count=0
failed_tests=
total_count=0

export OUTFILE="$PWD/results/setup"

clean >>$OUTFILE 2>&1

if ! get_version_info
then
    echo "get_version_info failed. See $OUTFILE for details."
    exit -1
fi

echo "Running against $MYSQL_FLAVOR $MYSQL_VERSION ($INNODB_FLAVOR $INNODB_VERSION)" |
  tee -a $OUTFILE

echo "Using '`basename $XB_BIN`' as xtrabackup binary" | tee -a $OUTFILE
echo | tee -a $OUTFILE

kill_leftovers >>$OUTFILE 2>&1
clean >>$OUTFILE 2>&1

source subunit.sh

echo "========================================================================"

for t in $tests
do
   total_count=$((total_count+1))

   printf "%-40s" $t
   subunit_start_test $t >> $SUBUNIT_OUT
   name=`basename $t .sh`
   export OUTFILE="$PWD/results/$name"
   export SKIPPED_REASON="$PWD/results/$name.skipped"
   bash $XTRACE_OPTION $t > $OUTFILE 2>&1
   rc=$?

   if [[ -z "$DEBUG" || -n "$force" ]]
   then
       kill_leftovers >>$OUTFILE 2>&1
       clean >>$OUTFILE 2>&1
   fi

   if [ $rc -eq 0 ]
   then
       echo "[passed]"
       subunit_pass_test $t >> $SUBUNIT_OUT
   elif [ $rc -eq $SKIPPED_EXIT_CODE ]
   then
       sreason=""
       test -r $SKIPPED_REASON && sreason=`cat $SKIPPED_REASON`
       echo "[skipped]    $sreason"
       subunit_skip_test $t >> $SUBUNIT_OUT
   else
       echo "[failed]"

       (
	   (echo "Something went wrong running $t. Exited with $rc";
	       echo; echo; cat $OUTFILE
	   ) | subunit_fail_test $t
       ) >> $SUBUNIT_OUT

       failed_count=$((failed_count+1))
       failed_tests="$failed_tests $t"
       result=1
       if [ -z "$force" ]
       then
	   break;
       fi
   fi       
done

echo "========================================================================"
echo

if [ $result -eq 1 ]
then
    echo
    echo "$failed_count/$total_count tests have failed: $failed_tests"
    echo "See results/ for detailed output"
    exit -1
fi
