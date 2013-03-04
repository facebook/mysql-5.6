# Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved. 
# Use is subject to license terms.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Suite 500, Boston, MA 02110-1335
# USA

use lib 'lib';
use lib "$ENV{RQG_HOME}/lib";
use lib 'randgen/lib';
#use lib 'randgen-alt/lib';

use strict;
use Carp;
use Cwd;
use DBI;
use File::Find;
use GenTest;
use GenTest::Random;
use GenTest::BzrInfo;
use POSIX;
use Sys::Hostname;

my ($basedir, $vardir, $tree, $test) = @ARGV;

# Which randgen variant do we use?
# When modifying this, remember to also modify the "use" statement above.
# (and ENV{PATH} for windows below)
my $randgen = 'randgen';
#my $randgen = 'randgen-alt';


#
# For further details about tests and recommended RQG options, see
# http://forge.mysql.com/wiki/RandomQueryGeneratorTests
#

print("==================== Starting $0 ====================\n");
# Print MTR-style output saying which test suite/mode this is for PB2 reporting.
# So far we only support running one test at a time.
print("##############################################################################\n");
print("# $test\n");
print("##############################################################################\n");

# Autoflush output buffers (needed when using POSIX::_exit())
$| = 1;

#
# Prepare ENV variables and other settings.
#

# Variable to indicate usage and location of grammar redefine files.
# Variable should remain undef if no redefine file is used.
my $redefine_file = undef;

# Local "installation" of MySQL 5.0. Default is for Unix hosts. See below for Windows.
my $basedirRelease50 = '/export/home/mysql-releases/mysql-5.0';

# Location of grammars and other test configuration files.
# Will use env variable RQG_CONF if set.
# Default is currently "conf" while using legacy setup.
# If not absolute path, it is relative to cwd at run time, which is the randgen directory.
my $conf = $ENV{RQG_CONF};
$conf = 'conf' if not defined $conf;

if (osWindows()) {
	# For tail, cdb, pscp.
	$ENV{PATH} = 'G:\pb2\scripts\randgen\bin;G:\pb2\scripts\bin;C:\Program Files\Debugging Tools for Windows (x86);'.$ENV{PATH};
	$ENV{_NT_SYMBOL_PATH} = 'srv*c:\\cdb_symbols*http://msdl.microsoft.com/download/symbols;cache*c:\\cdb_symbols';

	# For vlad
	#ENV{MYSQL_FULL_MINIDUMP} = 1;

	#system("date /T");
	#system("time /T");

	# Path to MySQL releases used for comparison runs.
	$basedirRelease50 = 'G:\mysql-releases\mysql-5.0.91-win32'; # loki06
} elsif (osSolaris()) {
	# For libmysqlclient
	$ENV{LD_LIBRARY_PATH}=$ENV{LD_LIBRARY_PATH}.':/export/home/pb2/scripts/lib/';

	# For DBI and DBD::mysql and XML::Writer on hosts with local setup.
	$ENV{PERL5LIB}=$ENV{PERL5LIB}.
		':/export/home/pb2/scripts/DBI-1.607/'.
		':/export/home/pb2/scripts/DBI-1.607/lib'.
		':/export/home/pb2/scripts/DBI-1.607/blib/arch/'.
		':/export/home/pb2/scripts/DBD-mysql-4.008/lib/'.
		':/export/home/pb2/scripts/DBD-mysql-4.008/blib/arch/'.
		':/export/home/pb2/scripts/XML-Writer-0.610/lib/site_perl/';
	
	# For c++filt
	$ENV{PATH} = $ENV{PATH}.':/opt/studio12/SUNWspro/bin';

	#system("uname -a");
	#system("date");

}

################################################################################
##
## subroutines
##
################################################################################

#
# Looks for a file name with the same name as the grammar mentioned in the
# command line string, except with a "_redefine.yy" suffix.
#
# The redefine file is expected to be in the same directory as the grammar 
# itself, at least for now.
# If such a file is not found, undef is returned.
#
# Input: Command string including "--grammar=...".
# Output: Filename of redefine file, to be used with --redefine option.
#         Returns undef if such a file is not found where expected.
#
sub redefine_filename ($){
    # Steps:
    #  1. Find out the grammar file name.
    #  2. Construct the expected redefine file name
    #     (replacing ".yy" with "_redefine.yy").
    #  3. Check if the redefine file exists.
    #  4. Return the redefine file name, or undef.
    #
    my $command_string = @_[0];
    if ($command_string =~ /(--grammar)=(.+)\s/m) {
        my $grammar_filename = $2;
        my $redefine_filename = $grammar_filename;
        $redefine_filename =~ s/\.yy/_redefine\.yy/;
        if (-e $redefine_filename) {
            say("Using redefine file ".$redefine_filename.".");
            print_special_comments($redefine_filename);
            return $redefine_filename;
        } else {
            say("No redefine file found for grammar ".$grammar_filename.".");
            print_special_comments($grammar_filename);
            return undef;
        }
    } else {
        croak("redefine_filename: Option --grammar not found in command. Command is: $command_string");
    }
}

#
# Looks in a file for comments of a certain format and prints them to standard
# output with some cruft to separate it from other output.
#
# Input: Filename (path)
#
sub print_special_comments ($){
    # Corresponding Unix command: grep '.*\(WL#\|Bug#\|Disabled\).*' grammar_file_redefine.yy
    
    my $filename = @_[0];
    my $pattern = "# +(WL#|Bug#|Disabled).*";
    my $pattern_new_entry = "# +(WL#|Bug#).+";
    # We assume that "Disabled" always comes after "Bug#" or "WL#" in files with
    # special comments with agreed structure. So in a line that matches the
    # first pattern we look for indicators of a new entry by using 
    # $pattern_new_entry (see below).

    say("Looking for special comments in grammar or redefine file...");
    say("Using pattern: ".$pattern);
    say("\n#### SPECIAL COMMENTS IN FILE $filename: ####");
    say();
    open FILE, "$filename" or croak("Unable to open file $filename");
    my $line;
    while ( <FILE> ) {
        $line = trim($_);
        if ($line =~ m{$pattern}) {
            # Add a blank line before all new entries, so that it is easier to 
            # distinguish separate entries in the test log. 
            if ($line =~ m{$pattern_new_entry}) {
                say("\n\t".$line);
            } else {
                say("\t".$line);
            }
        }
    }
    close FILE;
    say("");
    say("#### END OF SPECIAL COMMENTS ####\n");
    say("");
}


#
# Removes leading and trailing whitespace.
#
sub trim($)
{
    my $string = shift;
    $string =~ s/^\s+//;
    $string =~ s/\s+$//;
    return $string;
}


#
# Skips the test, displays reason (argument to the routine) quasi-MTR-style and
# exits with exit code 0.
#
# Example usage:
#   # This feature is not yet supported on Windows, so skip this test
#   skip_test("This feature/test does not support the Windows platform at this time");
#
# will appear in output as:
#   rpl_semisync                             [ skipped ] This feature/test does not support the Windows platform at this time
#
sub skip_test {
	my $reason = @_[0];
	my $message = "$test";
	# Using MTR-style output for the readers' convenience.
	# (at least 41 chars before "[ skipped ]")
	while (length $message < 40)
	{
		$message = $message.' ';
	}
	$message = $message." [ skipped ] ".$reason;
	print "$message\n";
	print localtime()." [$$] $0 will exit with exit status 0.\n";
	POSIX::_exit (0);
}

#
# Returns a random number between 1 and 499.
#
sub pick_random_port_range_id {
	my $prng = GenTest::Random->new( seed => time );
	return $prng->uint16(1,499);
}

#
# Searches recursively for a given file name under the given directory.
# Default top search directory is $basedir.
#
# Arg1 (mandatory): file name (excluding path)
# Arg2 (optional) : directory where search will start
#
# Returns full path to the directory where the file resides, if found.
# If more than one matching file is found, the directory of the first one found
# in a depth-first search will be returned.
# Returns undef if none is found.
#
sub findDirectory {
	my ($plugin_name, $dir) = @_;
	if (not defined $plugin_name) {
		carp("File name required as argument to subroutine findDirectory()");
	}
	if (not defined $dir) {
		$dir = $basedir;
	}
	my $fullPath;	# the result
	find(sub {
			# This subroutine is called for each file and dir it finds.
			# According to docs it does depth-first search.
			if ($_ eq $plugin_name) {
				$fullPath = $File::Find::dir if not defined $fullPath;
			}
			# any return value is ignored
		}, $dir);
	return $fullPath;
}

#
# Get the bzr branch ID from the pushbuild2 database (internal), based on the
# branch name ($tree variable).
#
# If the branch name (tree) is not found in the database, or we are unable to
# connect to the database, undef is returned.
#
sub get_pb2_branch_id {

	# First, check if the environment variable BRANCH_ID is set.
	if (defined $ENV{BRANCH_ID}) {
		return $ENV{BRANCH_ID};
	} else {
		# Disable db lookup for the time being due to issues on sparc32.
		# Remove this "else" block to enable
		return;
	}
	# Lookup by branch name. Get branch name from tree, which could be url.
	my $branch_name = $tree;
	if ($tree =~ m{/}) {
		# Found '/', assuming tree is URL.
		# Find last substring that is between a '/' and either end-of-string or a '/' followed by end of string.
		$tree =~ m{.*/([^/]+)($|/$)};
		$branch_name=$1;
	}

	my $dsn_pb2 = 'dbi:mysql:host=trollheim.no.oracle.com:port=3306:user=readonly:database=pushbuild2';
	my $SQL_getBranchId = "SELECT branch_id FROM branches WHERE branch_name = '$branch_name'";

	say("Using branch name $branch_name\n");
	say("Trying to connect to pushbuild2 database...\n");

	my $dbh = DBI->connect($dsn_pb2, undef, undef, {
		mysql_connect_timeout => 5,
		PrintError => 0,
		RaiseError => 0,
		AutoCommit => 0,
	} );

	if (not defined $dbh) {
		say("connect() to pushbuild2 database failed: ".$DBI::errstr."\n");
		return;
	}

	my $id = $dbh->selectrow_array($SQL_getBranchId);
	$dbh->disconnect;
	return $id;
}

#### end subroutines ###########################################################

# Find out active user name and mention it in the output to ease debugging.
my $username;
if (osLinux() || osSolaris()) {
    $username = $ENV{'LOGNAME'};
} else {
    $username = $ENV{'USERNAME'};
}

chdir($randgen);

say("Gathering info from the environment...");
# calling bzr usually takes a few seconds...
my $bzrinfo = GenTest::BzrInfo->new(
            dir => cwd()
);

say("===== Information on the host system: =====\n");
say(" - Local time  : ".localtime()."\n");
say(" - Hostname    : ".hostname()."\n");
say(" - Username    : ".$username."\n");
say(" - PID         : $$\n");
say(" - Working dir : ".cwd()."\n");
say(" - PATH        : ".$ENV{'PATH'}."\n");
say(" - Script arguments:\n");
say("       basedir = $basedir\n");
say("       vardir  = $vardir\n");
say("       tree    = $tree\n");
say("       test    = $test\n");
say("\n");
say("===== Information on the tested binaries (PB2): =====\n");
say(" - Branch URL  : ".$ENV{'BRANCH_SOURCE'});
say(" - Branch name : ".$ENV{'BRANCH_NAME'});
say(" - Revision    : ".$ENV{'PUSH_REVISION'});
say(" - Source      : ".$ENV{'SOURCE'});
say("===== Information on Random Query Generator version (bzr): =====\n");
say(" - Date (rev)  : ".$bzrinfo->bzrDate());
#say(" - Date (now) : ".$bzrinfo->bzrBuildDate());  # Shows current date, we already have that.
say(" - Revno       : ".$bzrinfo->bzrRevno());
say(" - Revision ID : ".$bzrinfo->bzrRevisionId());
say(" - Branch nick : ".$bzrinfo->bzrBranchNick());
say(" - Clean copy? : ". ($bzrinfo->bzrClean()? "Yes" : "No"));
say("\n");

# Test name:
#   In PB2, tests run via this script are prefixed with "rqg_" so that it is
#   easy to distinguish these tests from other "external" tests.
#   For a while we will support test names both with and without the prefix.
#   For this reason we strip off the "rqg_" prefix before continuing.
#   This also means that you cannot try to match against "rqg_" prefix in test
#   "definitions" (if statements) below.
my $test_name = $test;
my $test_suite_name = 'serverqa'; # used for xref reporting
$test =~ s/^rqg_//;	# test_name without prefix

# Server port numbers:
#
# If several instances of this script may run at the same time on the same
# host, port number conflicts may occur.
#
# If needed, use use a port range ID (integer) that is unique for this host at
# this time.
# This ID is used by the RQG framework to designate a port range to use for the
# test run. Passed to RQG using the MTR_BUILD_THREAD environment variable
# (this naming is a legacy from MTR, which is used by RQG to start the MySQL
# server).
#
# Solution: Use unique port range id per branch. Use "branch_id" as recorded
#           in PB2 database (guaranteed unique per branch).
# Potential issue 1: Unable to connect to pb2 database.
# Solution 1: Pick a random ID between 1 and some sensible number (e.g. 500).
# Potential issue 2: Clashing resources when running multiple pushes in same branch?
# Potential solution 2: Keep track of used ids in local file(s). Pick unused id.
#                       (not implemented yet)
#
# Currently (December 2009) PB2 RQG host should be running only one test at a
# time, so this should not be an issue, hence no need to set MTR_BUILD_THREAD.

#print("===== Determining port base id: =====\n");
my $port_range_id; # Corresponding to MTR_BUILD_THREAD in the MySQL MTR world.
# First, see if user has supplied us with a value for MTR_BUILD_THREAD:
$port_range_id = $ENV{MTR_BUILD_THREAD};
if (defined $port_range_id) {
	say("Environment variable MTR_BUILD_THREAD was already set.\n");
}
#else {
#	# try to obtain branch id, somehow
#	$port_range_id = get_pb2_branch_id();
#	if (not defined $port_range_id) {
#		print("Unable to get branch id. Picking a 'random' port base id...\n");
#		$port_range_id = pick_random_port_range_id();
#	} else {
#		print("Using pb2 branch ID as port base ID.\n");
#	}
#}

say("Configuring test...");
say("");

# Guess MySQL version. Some options we set later depend on this.
my $version50 = 0;      # 1 if 5.0.x, 0 otherwise. 
if( ($ENV{'BRANCH_SOURCE'} =~ m{-5\.0}io) || ($basedir =~ m{-5\.0}io) ) {
    say("Detected version 5.0.x, adjusting server options accordingly.");
    $version50 = 1;
}

my $cwd = cwd();

my $command;
my $engine;
my $rpl_mode;

if (($engine) = $test =~ m{(maria|falcon|innodb|myisam|pbxt)}io) {
	say("Detected that this test is about the $engine engine.");
}

if (($rpl_mode) = $test =~ m{(rbr|sbr|mbr|statement|mixed|row)}io) {
	say("Detected that this test is about replication mode $rpl_mode.");
	$rpl_mode = 'mixed' if $rpl_mode eq 'mbr';
	$rpl_mode = 'statement' if $rpl_mode eq 'sbr';
	$rpl_mode = 'row' if $rpl_mode eq 'rbr';
}

#
# Start defining tests. Test name can be whatever matches the regex in the if().
# TODO: Define less ambiguous test names to avoid accidental misconfiguration.
#
# Starting out with "legacy" Falcon tests.
#
if ($test =~ m{falcon_.*transactions}io ) {
	$command = '
		--grammar='.$conf.'/transactions/transactions.yy
		--gendata='.$conf.'/transactions/transactions.zz
		--mysqld=--falcon-consistent-read=1
		--mysqld=--transaction-isolation=REPEATABLE-READ
		--validator=DatabaseConsistency
		--mem
	';
} elsif ($test =~ m{falcon_.*durability}io ) {
	$command = '
		--grammar='.$conf.'/transactions/transaction_durability.yy
		--vardir1='.$vardir.'/vardir-'.$engine.'
		--vardir2='.$vardir.'/vardir-innodb
		--mysqld=--default-storage-engine='.$engine.'
		--mysqld=--falcon-checkpoint-schedule=\'1 1 1 1 1\'
		--mysqld2=--default-storage-engine=Innodb
		--validator=ResultsetComparator
	';
} elsif ($test =~ m{falcon_repeatable_read}io ) {
	$command = '
		--grammar='.$conf.'/transactions/repeatable_read.yy
		--gendata='.$conf.'/transactions/transactions.zz
		--mysqld=--falcon-consistent-read=1
		--mysqld=--transaction-isolation=REPEATABLE-READ
		--validator=RepeatableRead
		--mysqld=--falcon-consistent-read=1
		--mem
	';
} elsif ($test =~ m{falcon_chill_thaw_compare}io) {
	$command = '
	        --grammar='.$conf.'/engines/falcon/falcon_chill_thaw.yy
		--gendata='.$conf.'/engines/falcon/falcon_chill_thaw.zz
	        --mysqld=--falcon-record-chill-threshold=1K
	        --mysqld=--falcon-index-chill-threshold=1K 
		--threads=1
		--vardir1='.$vardir.'/chillthaw-vardir
		--vardir2='.$vardir.'/default-vardir
		--reporters=Deadlock,ErrorLog,Backtrace
	';
} elsif ($test =~ m{falcon_chill_thaw}io) {
	$command = '
	        --grammar='.$conf.'/engines/falcon/falcon_chill_thaw.yy
	        --mysqld=--falcon-index-chill-threshold=4K 
	        --mysqld=--falcon-record-chill-threshold=4K
	';
} elsif ($test =~ m{falcon_online_alter}io) {
	$command = '
	        --grammar='.$conf.'/engines/falcon/falcon_online_alter.yy
	';
} elsif ($test =~ m{falcon_ddl}io) {
	$command = '
	        --grammar='.$conf.'/engines/falcon/falcon_ddl.yy
	';
} elsif ($test =~ m{falcon_limit_compare_self}io ) {
	$command = '
		--grammar='.$conf.'/engines/falcon/falcon_nolimit.yy
		--threads=1
		--validator=Limit
	';
} elsif ($test =~ m{falcon_limit_compare_innodb}io ) {
	$command = '
		--grammar='.$conf.'/engines/falcon/limit_compare.yy
		--vardir1='.$vardir.'/vardir-falcon
		--vardir2='.$vardir.'/vardir-innodb
		--mysqld=--default-storage-engine=Falcon
		--mysqld2=--default-storage-engine=Innodb
		--threads=1
		--reporters=
	';
} elsif ($test =~ m{falcon_limit}io ) {
	$command = '
	        --grammar='.$conf.'/engines/falcon/falcon_limit.yy
		--mysqld=--loose-maria-pagecache-buffer-size=64M
	';
} elsif ($test =~ m{falcon_recovery}io ) {
	$command = '
	        --grammar='.$conf.'/engines/falcon/falcon_recovery.yy
		--gendata='.$conf.'/engines/falcon/falcon_recovery.zz
		--mysqld=--falcon-checkpoint-schedule="1 1 1 1 1"
	';
} elsif ($test =~ m{falcon_pagesize_32K}io ) {
	$command = '
		--grammar='.$conf.'/engines/falcon/falcon_pagesize.yy
		--mysqld=--falcon-page-size=32K
		--gendata='.$conf.'/engines/falcon/falcon_pagesize32K.zz
	';
} elsif ($test =~ m{falcon_pagesize_2K}io) {
	$command = '
		--grammar='.$conf.'/engines/falcon/falcon_pagesize.yy
		--mysqld=--falcon-page-size=2K
		--gendata='.$conf.'/engines/falcon/falcon_pagesize2K.zz
	';
} elsif ($test =~ m{falcon_select_autocommit}io) {
	$command = '
		--grammar='.$conf.'/engines/falcon/falcon_select_autocommit.yy
		--queries=10000000
	';
} elsif ($test =~ m{falcon_backlog}io ) {
	$command = '
		--grammar='.$conf.'/engines/falcon/falcon_backlog.yy
		--gendata='.$conf.'/engines/falcon/falcon_backlog.zz
		--mysqld=--transaction-isolation=REPEATABLE-READ
		--mysqld=--falcon-record-memory-max=10M
		--mysqld=--falcon-record-chill-threshold=1K
		--mysqld=--falcon-page-cache-size=128M
	';
} elsif ($test =~ m{falcon_compare_innodb}io ) {
        # Datatypes YEAR and TIME disabled in grammars due to Bug#45499 (InnoDB). 
        # Revert to falcon_data_types.{yy|zz} when that bug is resolved in relevant branches.
	$command = '
		--grammar='.$conf.'/engines/falcon/falcon_data_types_no_year_time.yy
		--gendata='.$conf.'/engines/falcon/falcon_data_types_no_year_time.zz
		--vardir1='.$vardir.'/vardir-falcon
		--vardir2='.$vardir.'/vardir-innodb
		--mysqld=--default-storage-engine=Falcon
		--mysqld2=--default-storage-engine=Innodb
		--threads=1
		--reporters=
	';
} elsif ($test =~ m{falcon_compare_self}io ) {
	$command = '
		--grammar='.$conf.'/engines/falcon/falcon_data_types.yy
		--gendata='.$conf.'/engines/falcon/falcon_data_types.zz
		--vardir1='.$vardir.'/'.$engine.'-vardir1
		--vardir2='.$vardir.'/'.$engine.'-vardir2
		--threads=1
		--reporters=
	';
#
# END OF FALCON-ONLY TESTS
#
} elsif ($test =~ m{innodb_repeatable_read}io ) {
	# Transactional test. See also falcon_repeatable_read.
	$command = '
		--grammar='.$conf.'/transactions/repeatable_read.yy
		--gendata='.$conf.'/transactions/transactions.zz
		--mysqld=--transaction-isolation=REPEATABLE-READ
		--validator=RepeatableRead
	';
} elsif ($test =~ m{(falcon|myisam)_blob_recovery}io ) {
	$command = '
		--grammar='.$conf.'/engines/falcon/falcon_blobs.yy
		--gendata='.$conf.'/engines/falcon/falcon_blobs.zz
		--duration=130
		--threads=1
		--reporters=Deadlock,ErrorLog,Backtrace,Recovery
	';
	if ($test =~ m{falcon}io) {
		# this option works with Falcon-enabled builds only
		$command = $command.'
			--mysqld=--falcon-page-cache-size=128M
		';
	}
} elsif ($test =~ m{(falcon|innodb|myisam)_many_indexes}io ) {
	$command = '
		--grammar='.$conf.'/engines/many_indexes.yy
		--gendata='.$conf.'/engines/many_indexes.zz
	';
} elsif ($test =~ m{(falcon|innodb|myisam)_tiny_inserts}io) {
	$command = '
		--gendata='.$conf.'/engines/tiny_inserts.zz
		--grammar='.$conf.'/engines/tiny_inserts.yy
		--queries=10000000
	';
} elsif ($test =~ m{innodb_transactions}io) {
	$command = '
		--grammar='.$conf.'/transactions/transactions.yy
		--gendata='.$conf.'/transactions/transactions.zz
		--mysqld=--transaction-isolation=REPEATABLE-READ
		--validator=DatabaseConsistency
	';
#
# END OF STORAGE ENGINE TESTS
#
# Keep the following tests in alphabetical order (based on letters in regex)
# for easy lookup.
#
} elsif ($test =~ m{^backup_.*?_simple}io) {
	$command = '
		--grammar='.$conf.'/backup/backup_simple.yy
		--reporters=Deadlock,ErrorLog,Backtrace
		--mysqld=--mysql-backup
	';
} elsif ($test =~ m{^backup_.*?_consistency}io) {
	$command = '
		--gendata='.$conf.'/backup/invariant.zz
		--grammar='.$conf.'/backup/invariant.yy
		--validator=Invariant
		--reporters=Deadlock,ErrorLog,Backtrace,BackupAndRestoreInvariant
		--mysqld=--mysql-backup
		--duration=600
		--threads=25
	';
} elsif ($test =~ m{dml_alter}io ) {
	$command = '
		--gendata='.$conf.'/engines/maria/maria.zz
		--grammar='.$conf.'/engines/maria/maria_dml_alter.yy
	';
} elsif ($test =~ m{^info_schema}io ) {
	$command = '
		--grammar='.$conf.'/runtime/information_schema.yy
		--threads=10
		--duration=300
		--mysqld=--log-output=file
	';
} elsif ($test =~ m{^mdl_stability}io ) {
	$command = '
		--grammar='.$conf.'/runtime/metadata_stability.yy
		--gendata='.$conf.'/runtime/metadata_stability.zz
		--validator=SelectStability,QueryProperties
		--engine=Innodb
		--mysqld=--innodb
		--mysqld=--default-storage-engine=Innodb
		--mysqld=--transaction-isolation=SERIALIZABLE
		--mysqld=--innodb-flush-log-at-trx-commit=2
		--mysqld=--loose-table-lock-wait-timeout=1
		--mysqld=--innodb-lock-wait-timeout=1
		--mysqld=--log-output=file
		--queries=1M
		--duration=600
	';
} elsif ($test =~ m{^mdl_deadlock}io ) {
    #
    # Should be same as mdl_stress (or mdl_stability, whichever has produced
    # the most deadlocks), except with higher (~default) lock_wait_timeouts.
    # The other variants have very low wait timeouts, making it difficult to
    # detect invalid deadlocks.
    # As per Feb-26-2010 default innodb-lock-wait-timeout=50 and
    # lock-wait-timeout=31536000 (bug#45225).
    # We may want to switch to real (implicit) defaults later.
    #
    # We use full grammar + redefine file for some branches, and custom grammar
    # with possibly no redefine file for others, hence this special grammar
    # logic.
    #
    my $grammar = "conf/runtime/WL5004_sql.yy";
    if ($tree =~ m{next-mr}i) {
        say("Custom grammar selected based on tree name ($tree).");
        $grammar = "conf/runtime/WL5004_sql_custom.yy";
    }
    $command = '
        --grammar='.$grammar.'
        --threads=10
        --queries=1M
        --duration=1200
        --mysqld=--innodb
        --mysqld=--innodb-lock-wait-timeout=50
        --mysqld=--lock-wait-timeout=31536000
        --mysqld=--log-output=file
        ';
} elsif ($test =~ m{^mdl_stress}io ) {
	# Seems like --gendata=conf/WL5004_data.zz unexplicably causes more test
	# failures, so let's leave this out of PB2 for the time being (pstoev).
	#
	# InnoDB should be loaded but the test is not per se for InnoDB, hence
	# no "innodb" in test name.
	#
	# We use full grammar + redefine file for some branches, and custom grammar
	# with possibly no redefine file for others, hence this special grammar
	# logic.
	#
	my $grammar = "conf/runtime/WL5004_sql.yy";
	if ($tree =~ m{next-mr}i) {
		say("Custom grammar selected based on tree name ($tree).");
		$grammar = "conf/runtime/WL5004_sql_custom.yy";
	}
	$command = '
		--grammar='.$grammar.'
		--threads=10
		--queries=1M
		--duration=1800
		--mysqld=--innodb
	';
	

#
# opt: Optimizer tests in "nice mode", primarily used for regression testing (5.1 and beyond).
#      By "nice mode" we mean relatively short duration and/or num of queries and fixed seed.
#
} elsif ($test =~ m{^opt_access_exp}io ) {
    # More queries drastically increases runtime.
    # We use a larger than default duration to allow even slower machines to do 
    # useful testing.
    # This test is for hitting as many table access methods as possible.
	$command = '
        --threads=1
        --queries=10K
        --gendata='.$conf.'/optimizer/range_access.zz
        --grammar='.$conf.'/optimizer/optimizer_access_exp.yy
        --duration=1200
	';
} elsif ($test =~ m{^opt_no_subquery(_trace)$}io ) {
	$command = '
        --threads=1
        --queries=100K
        --grammar='.$conf.'/optimizer/optimizer_no_subquery.yy
        --duration=1200
	';
} elsif ($test =~ m{^opt_no_subquery_compare_50}io ) {
    # Compares query results from 5.1 to those from 5.0.
    # We do not want the Shutdown reporter (default) here, in order to be able to compare dumps, so specify --reporters.
	$command = '
        --basedir1='.$basedir.'
        --basedir2='.$basedirRelease50.'
        --vardir1='.$vardir.'/vardir-bzr
        --vardir2='.$vardir.'/vardir-5.0
        --threads=1
        --queries=20K
        --grammar='.$conf.'/optimizer/optimizer_no_subquery_portable.yy
        --validator=ResultsetComparatorSimplify
        --reporters=Deadlock,ErrorLog,Backtrace
        --views
        --duration=1200
	';
} elsif ($test =~ m{^opt_range_access}io ) {
    # We should use a larger than default duration to allow even slower machines to do
    # useful testing.
    # 15K queries means runtime of ~40 mins on standard desktop hardware of Apr2010.
    # Used to allow 45 mins (2700s) of runtime.
    # This caused PB2 timeouts (30 min), so duration is now set to 25 minutes.
    # TODO: Adjust after implementing https://blueprints.launchpad.net/randgen/+spec/heartbeat-in-output
	$command = '
        --threads=1
        --queries=15K
        --gendata='.$conf.'/optimizer/range_access.zz
        --grammar='.$conf.'/optimizer/range_access.yy
        --duration=1500
	';
} elsif ($test =~ m{^opt_subquery}io ) {
    # Produces large and time consuming queries, so we use a larger than default 
    # duration to allow even slower machines to do useful testing.
	$command = '
        --threads=1
        --queries=75K
        --grammar='.$conf.'/optimizer/optimizer_subquery.yy
        --duration=1200
	';
} elsif ($test =~ m{^outer_join}io ) {
    # Any larger queries value than 30k used to cause a known/documented crash (5.1).
    # This seems to have been fixed by now.
    # Produces large and time consuming queries, so we use a larger than default
    # duration to allow even slower machines to do useful testing.
	$command = '
        --threads=1
        --queries=80K
        --gendata='.$conf.'/optimizer/outer_join.zz
        --grammar='.$conf.'/optimizer/outer_join.yy
        --duration=900
	';
#
# End of optimizer tests.
#
} elsif ($test =~ m{^partition_ddl}io ) {
	$command = '
		--grammar='.$conf.'/partitioning/partitions-ddl.yy
		--mysqld=--innodb
		--threads=1
		--queries=100K
	';
} elsif ($test =~ m{partn_pruning(|.valgrind)$}io ) {
	# reduced duration to half since gendata phase takes longer in this case
	$command = '
		--gendata='.$conf.'/partitioning/partition_pruning.zz
		--grammar='.$conf.'/partitioning/partition_pruning.yy
		--mysqld=--innodb
		--threads=1
		--queries=100000
		--duration=300
	';
} elsif ($test =~ m{^partn_pruning_compare_50}io) {
	$command = '
	--gendata='.$conf.'/partitioning/partition_pruning.zz
	--grammar='.$conf.'/partitioning/partition_pruning.yy
	--basedir1='.$basedir.'
	--basedir2='.$basedirRelease50.'
	--vardir1='.$vardir.'/vardir-bzr
	--vardir2='.$vardir.'/vardir-5.0
	--mysqld=--innodb
	--validators=ResultsetComparator
	--reporters=Deadlock,ErrorLog,Backtrace
	--threads=1
	--queries=10000
	--duration=300
	';
} elsif ($test =~ m{^rpl_.*?_simple}io) {
	# Not used; rpl testing needs adjustments (some of the failures this
	# produces are known replication issues documented in the manual).
	$command = '
		--gendata='.$conf.'/replication/replication_single_engine.zz
		--grammar='.$conf.'/replication/replication_simple.yy
		--mysqld=--log-output=table,file
	';
} elsif ($test =~ m{^rpl_.*?_complex}io) {
	# Not used; rpl testing needs adjustments (some of the failures this
	# produces are known replication issues documented in the manual).
	$command = '
		--gendata='.$conf.'/replication/replication_single_engine_pk.zz
		--grammar='.$conf.'/replication/replication.yy
		--mysqld=--log-output=table,file
		--mysqld=--innodb
	';
} elsif ($test =~ m{^rpl_semisync}io) {
	# --rpl_mode=default is used because the .YY file changes the binary log format dynamically.
	# --threads=1 is used to avoid any replication failures due to concurrent DDL.
	# --validator= line will remove the default replication Validator, which would otherwise
	#   report test failure when the slave I/O thread is stopped, which is OK in the context
	#   of this particular test.

	# Plugin file names and location vary between platforms.
	# See http://bugs.mysql.com/bug.php?id=49170 for details.
	# We search for the respective file names under basedir (recursively).
	# The first matching file that is found is used.
	# We assume that both master and slave plugins are in the same dir.
	# Unix file name extenstions other than .so may exist, but support for this
	# is not yet implemented here.
	my $plugin_dir;
	my $plugins;
	if (osWindows()) {
		my $master_plugin_name = "semisync_master.dll";
		$plugin_dir=findDirectory($master_plugin_name);
		if (not defined $plugin_dir) {
			carp "Unable to find semisync plugin $master_plugin_name!";
		}
		$plugins = 'rpl_semi_sync_master='.$master_plugin_name.';rpl_semi_sync_slave=semisync_slave.dll';
	} else {
		# tested on Linux and Solaris
		my $prefix;	# for Bug#48351
		my $master_plugin_name = "semisync_master.so";
		$plugin_dir=findDirectory($master_plugin_name);
		if (not defined $plugin_dir) {
			# Until fix for Bug#48351 is widespread it may happen
			# that the Unix plugin names are prefixed with "lib".
			# Remove this when no longer needed.
			$prefix = 'lib';
			$plugin_dir=findDirectory($prefix.$master_plugin_name);
			if (not defined $plugin_dir) {
				carp "Unable to find semisync plugin! ($master_plugin_name or $prefix$master_plugin_name)";
			}
		}
		$plugins = 'rpl_semi_sync_master='.$prefix.$master_plugin_name.':rpl_semi_sync_slave='.$prefix.'semisync_slave.so';
	}
	$command = '
		--gendata='.$conf.'/replication/replication_single_engine.zz
		--engine=InnoDB
		--grammar='.$conf.'/replication/replication_simple.yy
		--rpl_mode=default
		--mysqld=--plugin-dir='.$plugin_dir.'
		--mysqld=--plugin-load='.$plugins.'
		--mysqld=--rpl_semi_sync_master_enabled=1
		--mysqld=--rpl_semi_sync_slave_enabled=1
		--mysqld=--innodb
		--reporters=ReplicationSemiSync,Deadlock,Backtrace,ErrorLog
		--validators=None
		--threads=1
		--duration=300
		--queries=1M
	';
} elsif ($test =~ m{signal_resignal}io ) {
	$command = '
		--threads=10
		--queries=1M
		--duration=300
		--grammar='.$conf.'/runtime/signal_resignal.yy
		--mysqld=--max-sp-recursion-depth=10
	';
} elsif ($test =~ m{(innodb|maria|myisam)_stress}io ) {
	$command = '
		--grammar='.$conf.'/engines/maria/maria_stress.yy
	';
} elsif ($test =~ m{example}io ) {
    # this is here for the purpose testing this script
	$command = '
		--grammar='.$conf.'/examples/example.yy
        --threads=1
        --duration=40
        --queries=10000
	';
}else {
	say("[ERROR]: Test configuration for test name '$test' is not ".
		"defined in this script.\n");
	my $exitCode = 1;
	say("Will exit $0 with exit code $exitCode.\n");
	POSIX::_exit ($exitCode);
}

# Additional tests that are variants of the above defined tests:
#
# 1. Optimizer trace - all tests which name ends with "_trace":
#    Enable tracing and the OptimizerTraceParser validator.
#    NOTE: For applicable tests, must make sure regex in if checks above
#          will match the _trace suffix, otherwise the script will say
#          that the test configuration is not defined.
if ($test =~ m{.*_trace$}io ) {
    $command = $command.' --mysqld=--optimizer_trace="enabled=on"';
    $command = $command.' --validator=OptimizerTraceParser';
}


#
# Look for a redefine file for the grammar used, and add it to the command line
# if found. Also print special comments (e.g. about disabled parts) from the
# redefine file, alternatively the grammar file if no redefine file was found.
#
$redefine_file = redefine_filename($command);
$command = $command.' --redefine='.$redefine_file if defined $redefine_file;

#
# Specify some "default" Reporters if none have been specified already.
# The RQG itself also specifies some default values for some options if not set.
#
if ($command =~ m{--reporters}io) {
	# Reporters have already been specified	
} elsif ($test =~ m{rpl}io ) {
	# Don't include Recovery for replication tests, because
	$command = $command.' --reporters=Deadlock,ErrorLog,Backtrace';
} elsif ($test =~ m{falcon}io ) {
	# Include the Recovery reporter for Falcon tests in order to test
	# recovery by default after each such test.
	$command = $command.' --reporters=Deadlock,ErrorLog,Backtrace,Recovery,Shutdown';
	# Falcon-only options (avoid "unknown variable" warnings in non-Falcon builds)
	$command = $command.' --mysqld=--loose-falcon-lock-wait-timeout=5 --mysqld=--loose-falcon-debug-mask=2';
} else {
	# Default reporters for tests whose name does not contain "rpl" or "falcon"
	# Not using Shutdown reporter as it is not compatible with runall-new.pl (which shuts down its own server).
	$command = $command.' --reporters=Deadlock,ErrorLog,Backtrace';
}

#
# Other defaults...
#

if ($command !~ m{--duration}io ) {
	# Set default duration for tests where duration is not specified.
	# In PB2 we cannot run tests for too long since there are many branches
	# and many pushes (or other test triggers).
	# Setting it to 10 minutes for now.
	$command = $command.' --duration=600';
}

if ($command !~ m{--basedir}io ) {
	$command = $command." --basedir=\"$basedir\"";
}

if ($command !~ m{--vardir}io && $command !~ m{--mem}io ) {
	$command = $command." --vardir=\"$vardir\"";
}

# Logging to file is faster than table. And we want some form of logging.
# This option is not present in versions prior to 5.1.6, so skipping for 5.0.
if ($command !~ m{--log-output}io) {
	if (!$version50) {
		$command = $command.' --mysqld=--log-output=file';
	} 
}

# 1s to enable increased concurrency. NOTE: Removed in MySQL 5.5, Feb 2010.
if ($command !~ m{table-lock-wait-timeout}io) {
    $command = $command.' --mysqld=--loose-table-lock-wait-timeout=1';
}

# 1s to enable increased concurrency. NOTE: Added in MySQL 5.5, Feb 2010 (bug#45225).
# Default value in the server is 1 year.
if ($command !~ m{(--|--loose-)lock-wait-timeout}io) {
    $command = $command.' --mysqld=--loose-lock-wait-timeout=1';
}

# Decrease from default (50s) to 1s to enable increased concurrency.
if ($command !~ m{innodb-lock-wait-timeout}io) {
    $command = $command.' --mysqld=--loose-innodb-lock-wait-timeout=1';
}

if ($command !~ m{--queries}io) {
	$command = $command.' --queries=100000';
}

if (($command !~ m{--(engine|default-storage-engine)}io) && (defined $engine)) {
	$command = $command." --engine=$engine";
}

# if test name contains "innodb", add the --mysqld=--innodb and --engine=innodb 
# options if they are not there already.
if ( ($test =~ m{innodb}io) ){
	if ($command !~ m{mysqld=--innodb}io){
		$command = $command.' --mysqld=--innodb';
	}
	if ($command !~ m{engine=innodb}io){
		$command = $command.' --engine=innodb';
	}
}

if (($command !~ m{--rpl_mode}io)  && ($rpl_mode ne '')) {
	$command = $command." --rpl_mode=$rpl_mode";
}

# if test name contains (usually ends with) "valgrind", add the valgrind option to runall.pl
if ($test =~ m{valgrind}io){
	say("Detected that this test should enable valgrind instrumentation.\n");
	if (system("valgrind --version")) {
		say("  *** valgrind executable not found! Not setting --valgrind flag.\n");
	} else {
		$command = $command.' --valgrind';
	}
}
	
$command = "perl runall-new.pl --mysqld=--loose-skip-safemalloc ".$command;

### XML reporting setup START

# Pass test name to RQG, for reporting purposes
$command = $command." --testname=".$test_name;

# Enable XML reporting to TestTool.
# For now only on given hosts...
my %report_xml_from_hosts = (
    'loki06'   => '',
    'nanna21'  => '',
    'techra22' => '',
    'tor06-z1' => '',
    'tyr41'    => ''
);
my $hostname = hostname();
my $xmlfile;
my $delete_xmlfile = 0; # boolean indicator whether to delete local XML file.
if (exists $report_xml_from_hosts{$hostname}) {
    # We should enable XML reporting on this host...
    say("XML reporting to TestTool automatically enabled based on hostname.");
    # We need to write the XML to a file before sending to reporting framework.
    # This is done by specifying xml-output option.
    # TMPDIR should be set by Pushbuild to indicate a suitable location for temp files.
    # GenTest looks for other tmpdir alternatives.
    my $tmpdir = $ENV{'TMPDIR'} || tmpdir();
    if (length($tmpdir) > 1) {
        # tmpdir may or may not end with a file separator. Make sure it does.
        $tmpdir = $tmpdir.'/' if ($tmpdir =~ m{[^\/\\]+$});
        $xmlfile = $tmpdir.$test_name.'.xml';
    } else {
        # tmpdir not found. Write report to current directory.
        say("A suitable tmpdir was not found. Writing temporary files to current directory");
        # This file should be deleted after test end so that disks won't fill up.
        $delete_xmlfile = 1;
        $xmlfile = $test_name.'.xml';
    }
    # Enable XML reporting to TT (assuming this is not already enabled):
    $command = $command.' --xml-output='.$xmlfile.' --report-xml-tt';
    # Specify XML reporting transport type (not relying on defaults):
    # We assume SSH keys have been properly set up to enable seamless scp use.
    $command = $command.' --report-xml-tt-type=scp';
    # Specify destination for XML reports (not relying on defaults):
    $command = $command.' --report-xml-tt-dest=regin.no.oracle.com:/raid/xml_results/TestTool/xml/';
}
### XML reporting setup END


# Add env variable to specify unique port range to use to avoid conflicts.
# Trying not to do this unless actually needed.
if (defined $port_range_id) {
	say("MTR_BUILD_THREAD=$port_range_id\n");
	if (osWindows()) {
		$command = "set MTR_BUILD_THREAD=$port_range_id && ".$command;
	} else {
		$command = "MTR_BUILD_THREAD=$port_range_id ".$command;
	}
}

$command =~ s{[\r\n\t]}{ }sgio;
say("Running runall-new.pl...\n");
my $command_result = system($command);
# shift result code to the right to obtain the code returned from the called script
my $command_result_shifted = ($command_result >> 8);

# Report test result in an MTR fashion so that PB2 will see it and add to
# xref database etc.
# Format: TESTSUITE.TESTCASE 'TESTMODE' [ RESULT ]
# Example: ndb.ndb_dd_alter 'InnoDB plugin'     [ fail ]
# Not using TESTMODE for now.

my $full_test_name = $test_suite_name.'.'.$test_name;
# keep test statuses more or less vertically aligned
while (length $full_test_name < 40)
{
	$full_test_name = $full_test_name.' ';
}

if ($command_result_shifted > 0) {
	# test failed
	# Trying out marking a test as "experimental" by reporting exp-fail:
	# Mark all failures in next-mr-johnemb as experimental (temporary).
	if ($ENV{BRANCH_NAME} =~ m{mysql-next-mr-johnemb}) {
		print($full_test_name." [ exp-fail ]\n");
	} else {
		print($full_test_name." [ fail ]\n");
	}
	say('runall-new.pl failed with exit code '.$command_result_shifted."\n");
	say("Look above this message in the test log for failure details.\n");
} else {
	print($full_test_name." [ pass ]\n");
}

if ($delete_xmlfile && -e $xmlfile) {
    unlink $xmlfile;
    say("Temporary XML file $xmlfile deleted");
}

# Kill remaining mysqld processes.
# Assuming only one test run going on at the same time, and that all mysqld
# processes are ours.
say("Checking for remaining mysqld processes...\n");
if (osWindows()) {
	# assumes MS Sysinternals PsTools is installed in C:\bin
	# If you need to run pslist or pskill as non-Admin user, some adjustments
	# may be needed. See:
	#   http://blogs.technet.com/markrussinovich/archive/2007/07/09/1449341.aspx

	# Vardir may be relative path on windows, so convert to absolute path first:
	my $vardir_abspath = $vardir;
	if ($vardir !~ m/^[A-Z]:[\/\\]/i) {
	    # use basedir as prefix
	    $vardir_abspath = $basedir.'\\'.$vardir;
	}

	if (system('C:\bin\pslist mysqld') == 0) {
		say(" ^--- Found running mysqld process(es), to be killed if possible.\n");
		system('C:\bin\pskill mysqld > '.$vardir_abspath.'/pskill_mysqld.out 2>&1');
		system('C:\bin\pskill mysqld-nt > '.$vardir_abspath.'/pskill_mysqld-nt.out 2>&1');
	} else { say("  None found.\n"); }
	
} else {
	# Unix/Linux.
	# Avoid "bad argument count" messages from kill by checking if process exists first.
	if (system("pgrep mysqld") == 0) {
		say(" ^--- Found running mysqld process(es), to be killed if possible.\n");
		system("pgrep mysqld | xargs kill -15"); # "soft" kill
		sleep(5);
		if (system("pgrep mysqld > /dev/null") == 0) {
			# process is still around...
			system("pgrep mysqld | xargs kill -9"); # "hard" kill
		}
	} else { say("  None found.\n"); }
}

say(" [$$] $0 will exit with exit status ".$command_result_shifted."\n");
POSIX::_exit ($command_result_shifted);
