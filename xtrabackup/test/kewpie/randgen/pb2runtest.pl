#! /usr/bin/env perl

# Copyright (C) 2010 Sun Microsystems, Inc. All rights reserved.
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
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301
# USA

use lib 'lib';
use lib "$ENV{RQG_HOME}/lib";
use lib 'randgen/lib';

use strict;
use Carp;
use Cwd;
use DBI;
use File::Find;
use Getopt::Long;
use POSIX;
use Sys::Hostname;

use GenTest;
use GenTest::Properties;
use GenTest::Constants;

# TODO:
#  - clean up imports (use) not used.
#  - fix description after adjusting calling scripts (PB2)
#  - handle env vars RQG_HOME / RQG_CONF?
#  - add support for skipping tests and printing message (e.g.: If plugin not found)
#  - make basedir option a hash, skip basedirForRelease option?
#  - Reporting: Check and fix? More flexibility needed?
#  - propagate MTR_BUILD_THREAD / MTR_PORT_BASE, if needed?
#  - make vardir optional (mandatory vardir conflicts with --mem / MTR_MEM=1)
#  - add to help text (see sub help())
#  - add logic for process cleanup (left-over mysqlds), at least if Pushbuild
#  - more defaults?
#  - exit safely
#  - See also other TODO comments below.


################################################################################
#
# DRAFT - INCOMPLETE - DRAFT - INCOMPLETE - DRAFT
#
# This script is a wrapper around certain RQG functionality, primarily running
# well-defined RQG tests in a convenient way.
#
# The script accepts arguments which uniquely identify the test case to be run.
# From this identifier the script grabs information from a separate file which
# defines the test case, including various options to the RQG and MySQL.
# Then, the RQG's runall.pl script is executed with the options gathered from
# the test definition. Options may be overridden on the command line. 
# The runall.pl script takes care of starting the database server as well as
# executing the test itself.
#
# MySQL/Sun/Oracle notes:
#
# This script implements the interface between Pushbuild (automated testing
# infrastructure) and the Random Query Generator.
# The script is invoked by Pushbuild (PB2) slave instances when they have a job
# matching certain conditions.
# See tests_systemqa.py in pb2-trd-config repository for details.
#
# Pushbuild always provides the following arguments/oiptions to this script:
#   --basedir : Location of binaries to be tested
#   --vardir  : Location of temporary test files (logs, database, etc.)
#   --branch  : MySQL bazaar source code branch nick or other identifier
#               corresponding to the given binaries.
#   --config  : File specifying which test specification to use.
#               The test specification contains test name, options, etc.
#
# We hope to merge/integrate this with what is currently runall.pl and the
# new modules for server start that is being developed, eventually.
################################################################################

#
# See bottom of file for subroutines.
#


# Autoflush output buffers (needed when using POSIX::_exit())
$| = 1;

# Show in output when this script starts for ease of debugging when wrapped
# inside other scripts.
say("==================== Starting $0 ====================\n");

# Find out if this is Pushbuild (MySQL's automatic test system).
# We assume this is Pushbuild if environment variable PB2WORKDIR is set.
my $pushbuild = 1 if defined $ENV{'PB2WORKDIR'};



################################################################################
# Option handling
################################################################################

# We require --config to specify a test definition file.
# Properties.pm treats this option in a special way: As a file containing other
# options.
#
# Options set in this config file may be overridden on the command line.
# The resulting (merged) set of options is then passed to the RQG itself.
# Note: When given on command line, mysqld options should not include 'inner'
#       "--" prefix, e.g. "--mysqld=--lock-wait-timeout=2". Because prefixes
#       will be added automatically, specify "--mysqld=lock-wait-timeout=2"
#       or "--mysqld lock-wait-timeout=2" instead
#       (TODO: Add logic to handle inner -- on command line?).
#
# TODO: Have runall.pl use GenTest::Properties as well.
#
# Consider: Accept --test on command line and find test definition file based
#           on this (see parsing of such a file in lib/GenTest/App/Gendata.pm),
#           instead of having config as a special value, with path and all.

# Read options.
# old: my ($basedir, $vardir, $tree, $test) = @ARGV;
# new: We use GenTest::Properties mechanisms

my @ARGV_saved = @ARGV;

# Print full command line used for calling this script, so that it will appear
# in logs and can be repeated relatively easily.
# Avoiding say()'s prefixing to make it easy to copy & paste.
say("\nInbound command line was: ");
print("$0 \\ \n ".join(" \\ \n ", @ARGV_saved)."\n");


my $options = {};
my $opt_result = GetOptions($options,
    'basedir=s',
    'category=s',
    'config=s',     # The actual test definition, containing options.
    'grammar=s',
    'name=s',
    'vardir=s',     # TODO: Make optional?
    'basedirForRelease:s%',  # hash of paths to basedir for releases to test/compare
    'branch:s',     # TODO: Remove, get from ENV{BRANCH_NAME} instead if needed?
    'candidate:s',
    'dry_run',
    'duration:s',
    'help',
    'mysqld:s%',
    'threads:s',
    'verbose!'
    );
my $config = GenTest::Properties->new(
    options => $options,
    legal => [
        # Adds to the union of $options, $required and $defaults. Include here
        # options that may be specified in separate files, otherwise they
        # will be refused ($options only includes options actually given on
        # the command line).
        'basedir',
        'basedirForRelease',
        'candidate',
        'category',
        'description',
        'dry_run',
        'grammar',
        'help',
        'mysqld',
        'name',
        'threads',
        'vardir',
    ],
    required => [
        'basedir',          # Need to know what to test (binaries)
        'category',         # A test with unknown category is not well-defined.
        'config',           # File specifying a test. Used for forcing a well-defined approach.
        'grammar',          # No random queries without a grammar...
        'name'              # So that we can refer to it later and report results.
        ],
    defaults => {
        threads     => 1,       # Keep it simple by default.
        duration    => 300,     # Suitable for regular automated 'regression testing'
        candidate   => '',
        verbose     => '',
        basedirForRelease => {  # Locations of binaries for various releases
            '5.0' => osWindows() ?
                        'G:\mysql-releases\mysql-5.0.87-win32' :
                        '/export/home/mysql-releases/mysql-5.0'
        }
    },
    help => \&help
);

# Print help text if --help or no options were specified
$config->printHelp if not $opt_result or $config->help;



# List of options that are to be sent to the RQG runner script if defined.
my @randgen_opts_noprefix = (
    'basedir',
    'vardir',
    'grammar',
    'threads',
    'duration'
);



### Process options...

# Get list of options as string with appropriate prefixes.
my $mysqld_options = $config->genOpt('--mysqld=--', 'mysqld');
my $randgen_opts = $config->collectOpt('--', @randgen_opts_noprefix);
#say(my $all_non_complex_opts = $config->collectOpt('--'));

### Set helper variables
# candidate: Match only exact string 'true' (case insensitive)
# Candidate tests may be treated differently e.g. wrt. reporting.
my $candidate = 1 if $config->candidate =~ m{^true$}io;
my $spec_file = $config->config;      # file with test definition


################################################################################
# Prepare test, display info.
################################################################################

say("Loaded test definition from file " . $spec_file) if $spec_file;

#
# Prepare ENV variables and other settings.
#
setupPushbuildEnv() if ($pushbuild);

#
# Print easy-to-read info about the environment (useful for debugging etc.)
#
say("\n======== Information on the host system: ========");
say(" - Local time  : ".localtime());
say(" - Hostname    : ".hostname());
say(" - PID         : $$");
say(" - Working dir : ".cwd());
say(" - PATH        : ".$ENV{'PATH'});

say("\n======== Configuration: ========");
$config->printProps();  # TODO or not TODO? Useful or cluttering? Repeated in gentest?

# TODO: Only if this is a bzr branch and bzr is available...
#say("===== Information on Random Query Generator version (bzr): =====\n");
#system("bzr info");
#system("bzr version-info");
#say("\n");

# Print MTR-style output saying which test suite/mode this is.
# Needed for Pushbuild reporting. The Pushbuild parser requires a certain format
# of the output. 
# NOTE: So far we only support running one test at a time, so we only do this
#       here.
print("\n");
print("#" x 78 . "\n");
print("# " . $config->name . "\n");
print("#" x 78 . "\n");
print("\n");

## Debug/development output, temporary only?:
#say("Test description:");
#print("\n--------------------\n".$config->description . "\n---------------------\n\n");

say("This is a CANDIDATE TEST.") if $candidate;

# TODO: Remove? Require path to test file instead? (e.g. conf/runtime/rqg_info_schema.test)
# If not absolute path, it is relative to cwd at run time.
my $conf = $ENV{RQG_CONF};      # currently not used
$conf = 'conf' if not defined $conf;    


################################################################################
# Run the test
################################################################################

# Generate command-line
# TODO: Call/run runall or server+gentest using object/module calls instead.
my $cmd = 'perl runall.pl ' . $randgen_opts . ' ' . $mysqld_options;
$cmd =~ s{[\r\n\t]}{ }sgio;     # remove line breaks and tabs form command


if($config->dry_run) {
    # Display outbound command-line and exit
    $spec_file ? say("Dry run, pretending to run test defined by ".$spec_file."...")
               : say("Dry run, pretending to run test defined on command-line...");
    say($cmd);
    say("\n$0 Done.");
    safe_exit();
}


# run for real, process results, report, etc.
say("Command line:");
say($cmd);
say("");

my $command_result = system($cmd);
# shift result code to the right to obtain the code returned from the called script
my $command_result_shifted = ($command_result >> 8);


################################################################################
# Process results and report.
################################################################################

# TODO

### Report test result in an MTR fashion.
### This is done so that Pushbuild will see it, parse it and add to xref database
### etc.
### Format: TESTSUITE.TESTCASE 'TESTMODE' [ RESULT ]
### Example: ndb.ndb_dd_alter 'InnoDB plugin'     [ fail ]
### Not using TESTMODE for now.

my $full_test_name = $config->category.'.'.$config->name;
# keep test statuses more or less vertically aligned
while (length $full_test_name < 40)
{
	$full_test_name = $full_test_name.' ';
}

if ($command_result_shifted > 0) {
	# test failed
	# Marking candidate test as "experimental" by reporting exp-fail status,
    # as used by Pushbuild. TODO: Support other variations if needed.
	if ($candidate) {
		print($full_test_name." [ exp-fail ]\n");
	} else {
		print($full_test_name." [ fail ]\n");
	}
	say('Command failed with exit code '.$command_result_shifted);
	say('Look above this message in the test log for failure details.');
} else {
	print($full_test_name." [ pass ]\n");
}

################################################################################
# Clean up
################################################################################

say("\n$0 Done\n");




################################################################################
# Subroutines
################################################################################

###
### Displays help text. TODO: List more / all options?
###
sub help {

    print <<EOF

###### Help for $0 - Testing via random query generation ######

 Options:

    --basedir   : Top-level directory (basedir) of the database installation to be tested.
    --config    : Path to configuration file specifying a set of options, e.g. a test definition (.test).
                  Options specified in the config file can be overridden on the command-line.
    --dry_run   : Do not actually run the test, but process options and show resulting commands.
    --help      : Display this help message.

    Example command-line:
        $0 --config=conf/examples/example.test --basedir=/home/user/mysql-trunk --duration=60

        This says "Run the test example.test found here... and set duration to 60 seconds".
        Other required options are set in the example.test file.

    Example .test file (for --config option):

        {
            name        => 'rqg_example',
            category    => 'example',
            description => 'Example test showing how to use the RQG',
            grammar     => 'conf/examples/example.yy',
            threads     => 5,
            mysqld      => {
                'log-output' => 'file'
            }
        }

    Command line options override options in --config file.
    
EOF
	;
	safe_exit(1);
}


###
### Sets up environment variables and other stuff required when running in
### MySQL's Pushbuild environment.
###
sub setupPushbuildEnv {
    if (osWindows()) {
        # For tail and for cdb
        # TODO: Remove randgen\bin, not used?
        $ENV{PATH} =
            'G:\pb2\scripts\randgen\bin'.
            ';G:\pb2\scripts\bin;C:\Program Files\Debugging Tools for Windows (x86)'.
            ';'.$ENV{PATH};

        # For cdb (stack traces)
        $ENV{_NT_SYMBOL_PATH} = 'srv*c:\\cdb_symbols*http://msdl.microsoft.com/download/symbols;cache*c:\\cdb_symbols';

        # For vlad (~2008-09)
        #ENV{MYSQL_FULL_MINIDUMP} = 1;

    } elsif (osSolaris()) {
        # For libmysqlclient
        $ENV{LD_LIBRARY_PATH} =
            $ENV{LD_LIBRARY_PATH}.
            ':/export/home/pb2/scripts/lib/';

        # For DBI and DBD::mysql (on hosts with special Perl setup)
        $ENV{PERL5LIB} =
            $ENV{PERL5LIB}.
            ':/export/home/pb2/scripts/DBI-1.607/'.
            ':/export/home/pb2/scripts/DBI-1.607/lib'.
            ':/export/home/pb2/scripts/DBI-1.607/blib/arch/'.
            ':/export/home/pb2/scripts/DBD-mysql-4.008/lib/'.
            ':/export/home/pb2/scripts/DBD-mysql-4.008/blib/arch/';

        # For c++filt
        $ENV{PATH} = $ENV{PATH}.':/opt/studio12/SUNWspro/bin';
    }

    # We currently assume current working dir is a 'scripts' directory
    # containing a copy of the random query generator.
    # Make sure current working dir is the top-level randgen dir.
    # TODO: Check if really needed.
    chdir('randgen');
}
