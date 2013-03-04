#!/usr/bin/perl

# Copyright (c) 2008, 2011 Oracle and/or its affiliates. All rights reserved.
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

#
# This script executes the following sequence
#
# $ mysql-test-run.pl --start-and-exit with replication
# $ gentest.pl --gendata
# $ diff master slave
#
#

use lib 'lib';
use lib "$ENV{RQG_HOME}/lib";
use strict;
use GenTest;
use Carp;

my $logger;
eval
{
    require Log::Log4perl;
    Log::Log4perl->import();
    $logger = Log::Log4perl->get_logger('randgen.gentest');
};

use GenTest::BzrInfo;

$| = 1;
if (osWindows()) {
	$SIG{CHLD} = "IGNORE";
}

if (defined $ENV{RQG_HOME}) {
        if (osWindows()) {
                $ENV{RQG_HOME} = $ENV{RQG_HOME}.'\\';
        } else {
                $ENV{RQG_HOME} = $ENV{RQG_HOME}.'/';
        }
}

use Getopt::Long;
use GenTest::Constants;
use DBI;
use Cwd;

my $database = 'test';
my @master_dsns;

my ($gendata, $skip_gendata, @basedirs, @mysqld_options, @vardirs, $rpl_mode,
    $engine, $help, $debug, $validators, $reporters, $grammar_file, $skip_recursive_rules,
    $redefine_file, $seed, $mask, $mask_level, $no_mask, $mem, $rows,
    $varchar_len, $xml_output, $valgrind, $valgrind_xml, $views,
    $start_dirty, $filter, $build_thread, $testname, $report_xml_tt,
    $report_xml_tt_type, $report_xml_tt_dest, $notnull, $sqltrace,
    $lcov, $transformers, $logfile, $logconf, $report_tt_logdir,$querytimeout,
    $short_column_names, $strict_fields, $freeze_time);

my $threads = my $default_threads = 10;
my $queries = my $default_queries = 1000;
my $duration = my $default_duration = 3600;

my @ARGV_saved = @ARGV;

my $opt_result = GetOptions(
	'mysqld=s@' => \$mysqld_options[0],
	'mysqld1=s@' => \$mysqld_options[0],
	'mysqld2=s@' => \$mysqld_options[1],
	'basedir=s' => \$basedirs[0],
	'basedir1=s' => \$basedirs[0],
	'basedir2=s' => \$basedirs[1],
	'vardir=s' => \$vardirs[0],
	'vardir1=s' => \$vardirs[0],
	'vardir2=s' => \$vardirs[1],
	'rpl_mode=s' => \$rpl_mode,
	'engine=s' => \$engine,
	'grammar=s' => \$grammar_file,
	'skip-recursive-rules' => \$skip_recursive_rules,	
	'redefine=s' => \$redefine_file,
	'threads=i' => \$threads,
	'queries=s' => \$queries,
	'duration=i' => \$duration,
	'help' => \$help,
	'debug' => \$debug,
	'validators:s@' => \$validators,
    'transformers:s@' =>\$transformers,
	'reporters:s@' => \$reporters,
	'report-xml-tt' => \$report_xml_tt,
	'report-xml-tt-type=s' => \$report_xml_tt_type,
	'report-xml-tt-dest=s' => \$report_xml_tt_dest,
	'gendata:s' => \$gendata,
	'skip-gendata' => \$skip_gendata,
	'notnull' => \$notnull,
	'short_column_names' => \$short_column_names,
	'strict_fields' => \$strict_fields,
	'freeze_time' => \$freeze_time,
	'seed=s' => \$seed,
	'mask=i' => \$mask,
    'mask-level=i' => \$mask_level,
	'no-mask' => \$no_mask,
	'mem' => \$mem,
	'rows=s' => \$rows,
	'varchar-length=i' => \$varchar_len,
	'xml-output=s'	=> \$xml_output,
	'valgrind'	=> \$valgrind,
	'valgrind-xml'	=> \$valgrind_xml,
	'views:s'	=> \$views,
	'sqltrace:s' => \$sqltrace,
	'start-dirty'	=> \$start_dirty,
	'filter=s'	=> \$filter,
    'mtr-build-thread=i' => \$build_thread,
    'testname=s' => \$testname,
	'lcov' => \$lcov,
    'logfile=s' => \$logfile,
    'logconf=s' => \$logconf,
    'report-tt-logdir=s' => \$report_tt_logdir,
    'querytimeout=i' => \$querytimeout
);

if (defined $logfile && defined $logger) {
    setLoggingToFile($logfile);
} else {
    if (defined $logconf && defined $logger) {
        setLogConf($logconf);
    }
}

$ENV{RQG_DEBUG} = 1 if defined $debug;

$validators = join(',', @$validators) if defined $validators;
$reporters = join(',', @$reporters) if defined $reporters;
$transformers = join(',', @$transformers) if defined $transformers;

if (!$opt_result) {
	exit(1);
} elsif ($help) {
	help();
	exit(0);
} elsif ($basedirs[0] eq '') {
	say("No basedir provided via --basedir.");
	exit(0);
} elsif (not defined $grammar_file) {
	say("No grammar file provided via --grammar");
	exit(0);
}

# --sqltrace may have a string value (optional). 
# Allowed values for --sqltrace:
my %sqltrace_legal_values = (
    'MarkErrors' => 1 # Prefixes invalid SQL statements for easier post-processing
);

# If no value is given, GetOpt will assign the value '' (empty string).
if (length($sqltrace) > 0) {
    # A value is given, check if it is legal.
    if (not exists $sqltrace_legal_values{$sqltrace}) {
        say("Invalid value for --sqltrace option: '$sqltrace'");
        say("Valid values are: ".join(', ', keys(%sqltrace_legal_values)));
        say("No value means that default/plain sqltrace will be used.");
        exit(STATUS_ENVIRONMENT_FAILURE);
    }
}

say("Copyright (c) 2008,2011 Oracle and/or its affiliates. All rights reserved. Use is subject to license terms.");
say("Please see http://forge.mysql.com/wiki/Category:RandomQueryGenerator for more information on this test framework.");
say("Starting: $0 ".join(" ", @ARGV_saved));

#
# Calculate master and slave ports based on MTR_BUILD_THREAD (MTR
# Version 1 behaviour)
#

if (not defined $build_thread) {
    if (defined $ENV{MTR_BUILD_THREAD}) {
        $build_thread = $ENV{MTR_BUILD_THREAD}
    } else {
        $build_thread = DEFAULT_MTR_BUILD_THREAD;
    }
}

if ( $build_thread eq 'auto' ) {
    say ("Please set the environment variable MTR_BUILD_THREAD to a value <> 'auto' (recommended) or unset it (will take the value ".DEFAULT_MTR_BUILD_THREAD.") ");
    exit (STATUS_ENVIRONMENT_FAILURE);
}

my $master_port = 10000 + 10 * $build_thread;
my $slave_port = 10000 + 10 * $build_thread + 2;
my @master_ports = ($master_port,$slave_port);

say("master_port : $master_port slave_port : $slave_port master_ports : @master_ports MTR_BUILD_THREAD : $build_thread ");

$ENV{MTR_BUILD_THREAD} = $build_thread;

#
# If the user has provided two vardirs and one basedir, start second
# server using the same basedir
#


if (
	($vardirs[1] ne '') && 
	($basedirs[1] eq '')
) {
	$basedirs[1] = $basedirs[0];	
}

foreach my $dir (cwd(), @basedirs) {
# calling bzr usually takes a few seconds...
    if (defined $dir) {
        my $bzrinfo = GenTest::BzrInfo->new(
            dir => $dir
            ); 
        my $revno = $bzrinfo->bzrRevno();
        my $revid = $bzrinfo->bzrRevisionId();
        
        if ((defined $revno) && (defined $revid)) {
            say("$dir Revno: $revno");
            say("$dir Revision-Id: $revid");
        } else {
            say($dir.' does not look like a bzr branch, cannot get revision info.');
        } 
    }
}



if (
	($mysqld_options[1] ne '') && 
	($basedirs[1] eq '')
) {
	$basedirs[1] = $basedirs[0];	
}

#
# If the user has provided identical basedirs and vardirs, warn of a potential overlap.
#

if (
	($basedirs[0] eq $basedirs[1]) &&
	($vardirs[0] eq $vardirs[1]) &&
	($rpl_mode eq '')
) {
	die("Please specify either different --basedir[12] or different --vardir[12] in order to start two MySQL servers");
}

#
# If RQG_HOME is set, prepend it to config files if they can not be found without it 
#

$gendata = $ENV{RQG_HOME}.'/'.$gendata if defined $gendata && defined $ENV{RQG_HOME} && ! -e $gendata;
$grammar_file = $ENV{RQG_HOME}.'/'.$grammar_file if defined $grammar_file && defined $ENV{RQG_HOME} && ! -e $grammar_file;
$redefine_file = $ENV{RQG_HOME}.'/'.$redefine_file if defined $redefine_file && defined $ENV{RQG_HOME} && ! -e $redefine_file;

my $cwd = cwd();

if ($lcov) {
	unlink(tmpdir()."/lcov-rqg.info");
	system("lcov --directory $basedirs[0] --zerocounters");
}

#
# Start servers. Use rpl_alter if replication is needed.
#
	
foreach my $server_id (0..1) {
	next if $basedirs[$server_id] eq '';

	if (
		($server_id == 0) ||
		($rpl_mode eq '') 
	) {
		$master_dsns[$server_id] = "dbi:mysql:host=127.0.0.1:port=".$master_ports[$server_id].":user=root:database=".$database;
	}

	my @mtr_options;
	push @mtr_options, lc("--mysqld=--$engine") if defined $engine && $engine !~ m{myisam|memory|heap|aria}sio;

	push @mtr_options, "--mem" if defined $mem;
	if ((defined $valgrind) || (defined $valgrind_xml)) {
		push @mtr_options, "--valgrind";
		if (defined $valgrind_xml) {
			push @mtr_options, "--valgrind-option='--xml=yes'";
			if (defined $vardirs[$server_id]) {
				push @mtr_options, "--valgrind-option='--xml-file=".$vardirs[$server_id]."/log/valgrind.xml'";
			} else {
				push @mtr_options, "--valgrind-option='--xml-file=".$basedirs[$server_id]."/mysql-test/var/log/valgrind.xml'";
			}
		}
	}
			
	push @mtr_options, "--skip-ndb";
	push @mtr_options, "--mysqld=--core-file";
	push @mtr_options, "--mysqld=--loose-new";
	push @mtr_options, "--mysqld=--default-storage-engine=$engine" if defined $engine;
	push @mtr_options, "--mysqld=--sql-mode=no_engine_substitution" if join(' ', @ARGV_saved) !~ m{sql-mode}io;
	push @mtr_options, "--mysqld=--relay-log=slave-relay-bin";
	push @mtr_options, "--mysqld=--loose-innodb";
	push @mtr_options, "--mysqld=--loose-falcon-debug-mask=2";
	push @mtr_options, "--mysqld=--secure-file-priv=";		# Disable secure-file-priv that mtr enables.
	push @mtr_options, "--mysqld=--max-allowed-packet=16Mb";	# Allow loading bigger blobs
	push @mtr_options, "--mysqld=--loose-innodb-status-file=1";
	push @mtr_options, "--mysqld=--master-retry-count=65535";
	push @mtr_options, "--mysqld=--loose-debug-assert-if-crashed-table";
	push @mtr_options, "--mysqld=--loose-debug-assert-on-error";
	push @mtr_options, "--mysqld=--skip-name-resolve";

	push @mtr_options, "--start-dirty" if defined $start_dirty;
	push @mtr_options, "--gcov" if $lcov;

	if (($rpl_mode ne '') && ($server_id != 0)) {
		# If we are running in replication, and we start the slave separately (because it is a different binary)
		# add a few options that allow the slave and the master to be distinguished and SHOW SLAVE HOSTS to work
		push @mtr_options, "--mysqld=--server-id=".($server_id + 1);
		push @mtr_options, "--mysqld=--report-host=127.0.0.1";
		push @mtr_options, "--mysqld=--report-port=".$master_ports[$server_id];
	}

	my $mtr_path = $basedirs[$server_id].'/mysql-test/';
	chdir($mtr_path) or croak "unable to chdir() to $mtr_path: $!";
	
	push @mtr_options, "--vardir=$vardirs[$server_id]" if defined $vardirs[$server_id];
	push @mtr_options, "--master_port=".$master_ports[$server_id];

	if (defined $mysqld_options[$server_id]) {
		foreach my $mysqld_option (@{$mysqld_options[$server_id]}) {
			push @mtr_options, '--mysqld="'.$mysqld_option.'"';
		}
	}

	if (
		($rpl_mode ne '') &&
		($server_id == 0) &&
		(not defined $vardirs[1]) &&
		(not defined $mysqld_options[1])
	) {
		push @mtr_options, 'rpl_alter';
		push @mtr_options, "--slave_port=".$slave_port;
	} elsif ($basedirs[$server_id] =~ m{(^|[-/ ])5\.0}sgio) {
		say("Basedir implies server version 5.0. Will not use --start-and-exit 1st");
		# Do nothing, test name "1st" does not exist in 5.0
	} else {
		push @mtr_options, '1st';
	}

	$ENV{MTR_VERSION} = 1;
#	my $out_file = "/tmp/mtr-".$$."-".$server_id.".out";
	my $mtr_command = "perl mysql-test-run.pl --start-and-exit ".join(' ', @mtr_options)." 2>&1";
	say("Running $mtr_command .");

	my $vardir = $vardirs[$server_id] || $basedirs[$server_id].'/mysql-test/var';

	open (MTR_COMMAND, '>'.$mtr_path.'/mtr_command') or say("Unable to open mtr_command: $!");
	print MTR_COMMAND $mtr_command;
	close MTR_COMMAND;

	my $mtr_status = system($mtr_command);

	if ($mtr_status != 0) {
#		system("cat $out_file");
		system("cat \"$vardir/log/master.err\"");
		exit_test(STATUS_ENVIRONMENT_FAILURE);
	}
#	unlink($out_file);
	
	if ((defined $master_dsns[$server_id]) && (defined $engine)) {
		my $dbh = DBI->connect($master_dsns[$server_id], undef, undef, { RaiseError => 1 } );
		$dbh->do("SET GLOBAL storage_engine = '$engine'");
	}
}

chdir($cwd);

my $master_dbh = DBI->connect($master_dsns[0], undef, undef, { RaiseError => 1 } );

if ($rpl_mode) {
	my $slave_dsn = "dbi:mysql:host=127.0.0.1:port=".$slave_port.":user=root:database=".$database;
	my $slave_dbh = DBI->connect($slave_dsn, undef, undef, { RaiseError => 1 } );

	say("Establishing replication, mode $rpl_mode ...");

	my ($foo, $master_version) = $master_dbh->selectrow_array("SHOW VARIABLES LIKE 'version'");

	if (($master_version !~ m{^5\.0}sio) && ($rpl_mode ne 'default')) {
		$master_dbh->do("SET GLOBAL BINLOG_FORMAT = '$rpl_mode'");
		$slave_dbh->do("SET GLOBAL BINLOG_FORMAT = '$rpl_mode'");
	}

	$slave_dbh->do("STOP SLAVE");

	$slave_dbh->do("SET GLOBAL storage_engine = '$engine'") if defined $engine;

	$slave_dbh->do("CHANGE MASTER TO
		MASTER_PORT = $master_ports[0],
		MASTER_HOST = '127.0.0.1',
               MASTER_USER = 'root',
               MASTER_CONNECT_RETRY = 1
	");

	$slave_dbh->do("START SLAVE");
}

#
# Run actual queries
#

my @gentest_options;

push @gentest_options, "--start-dirty" if defined $start_dirty;
push @gentest_options, "--gendata=$gendata" if not defined $skip_gendata;
push @gentest_options, "--notnull" if defined $notnull;
push @gentest_options, "--short_column_names" if defined $short_column_names;
push @gentest_options, "--strict_fields" if defined $strict_fields;
push @gentest_options, "--freeze_time" if defined $freeze_time;
push @gentest_options, "--engine=$engine" if defined $engine;
push @gentest_options, "--rpl_mode=$rpl_mode" if defined $rpl_mode;
push @gentest_options, map {'--validator='.$_} split(/,/,$validators) if defined $validators;
push @gentest_options, map {'--reporter='.$_} split(/,/,$reporters) if defined $reporters;
push @gentest_options, map {'--transformer='.$_} split(/,/,$transformers) if defined $transformers;
push @gentest_options, "--threads=$threads" if defined $threads;
push @gentest_options, "--queries=$queries" if defined $queries;
push @gentest_options, "--duration=$duration" if defined $duration;
push @gentest_options, "--dsn=$master_dsns[0]" if defined $master_dsns[0];
push @gentest_options, "--dsn=$master_dsns[1]" if defined $master_dsns[1];
push @gentest_options, "--grammar=$grammar_file";
push @gentest_options, "--skip-recursive-rules" if defined $skip_recursive_rules;
push @gentest_options, "--redefine=$redefine_file" if defined $redefine_file;
push @gentest_options, "--seed=$seed" if defined $seed;
push @gentest_options, "--mask=$mask" if ((defined $mask) && (not defined $no_mask));
push @gentest_options, "--mask-level=$mask_level" if defined $mask_level;
push @gentest_options, "--rows=$rows" if defined $rows;
push @gentest_options, "--views=$views" if defined $views;
push @gentest_options, "--varchar-length=$varchar_len" if defined $varchar_len;
push @gentest_options, "--xml-output=$xml_output" if defined $xml_output;
push @gentest_options, "--report-xml-tt" if defined $report_xml_tt;
push @gentest_options, "--report-xml-tt-type=$report_xml_tt_type" if defined $report_xml_tt_type;
push @gentest_options, "--report-xml-tt-dest=$report_xml_tt_dest" if defined $report_xml_tt_dest;
push @gentest_options, "--debug" if defined $debug;
push @gentest_options, "--filter=$filter" if defined $filter;
push @gentest_options, "--valgrind" if defined $valgrind;
push @gentest_options, "--valgrind-xml" if defined $valgrind_xml;
push @gentest_options, "--testname=$testname" if defined $testname;
push @gentest_options, "--sqltrace".(length($sqltrace)>0 ? "=$sqltrace" : '' ) if defined $sqltrace;
push @gentest_options, "--logfile=$logfile" if defined $logfile;
push @gentest_options, "--logconf=$logconf" if defined $logconf;
push @gentest_options, "--report-tt-logdir=$report_tt_logdir" if defined $report_tt_logdir;
push @gentest_options, "--querytimeout=$querytimeout" if defined $querytimeout;

# Push the number of "worker" threads into the environment.
# lib/GenTest/Generator/FromGrammar.pm will generate a corresponding grammar element.
$ENV{RQG_THREADS}= $threads;

my $gentest_result = system("perl ".($Carp::Verbose?"-MCarp=verbose ":"").
                            "$ENV{RQG_HOME}gentest.pl ".join(' ', @gentest_options)) >> 8;
say("gentest.pl exited with exit status ".status2text($gentest_result). " ($gentest_result)");

if ($lcov) {
	say("Trying to generate a genhtml lcov report in ".tmpdir()."/rqg-lcov-$$ ...");
	system("lcov --quiet --directory $basedirs[0] --capture --output-file ".tmpdir()."/lcov-rqg.info");
	system("genhtml --quiet --no-sort --output-directory=".tmpdir()."/rqg-lcov-$$ ".tmpdir()."/lcov-rqg.info");
	say("genhtml lcov report may have been generated in ".tmpdir()."/rqg-lcov-$$ .");

}	

exit_test($gentest_result);

sub help {

	print <<EOF
Copyright (c) 2008,2011 Oracle and/or its affiliates. All rights reserved. Use is subject to license terms.

$0 - Run a complete random query generation test, including server start with replication and master/slave verification
    
    Options related to one standalone MySQL server:

    --basedir   : Specifies the base directory of the stand-alone MySQL installation;
    --mysqld    : Options passed to the MySQL server
    --vardir    : Optional. (default \$basedir/mysql-test/var);

    Options related to two MySQL servers

    --basedir1  : Specifies the base directory of the first MySQL installation;
    --basedir2  : Specifies the base directory of the second MySQL installation;
    --mysqld1   : Options passed to the first MySQL server
    --mysqld2   : Options passed to the second MySQL server
    --vardir1   : Optional. (default \$basedir1/mysql-test/var);
    --vardir2   : Optional. (default \$basedir2/mysql-test/var);

    General options

    --grammar   : Grammar file to use when generating queries (REQUIRED);
    --redefine  : Grammar file to redefine and/or add rules to the given grammar
    --rpl_mode  : Replication type to use (statement|row|mixed) (default: no replication);
    --vardir1   : Optional.
    --vardir2   : Optional. 
    --engine    : Table engine to use when creating tables with gendata (default no ENGINE in CREATE TABLE);
    --threads   : Number of threads to spawn (default $default_threads);
    --queries   : Number of queries to execute per thread (default $default_queries);
    --duration  : Duration of the test in seconds (default $default_duration seconds);
    --validator : The validators to use
    --reporter  : The reporters to use
    --transformer: The transformers to use (turns on --validator=transformer). Accepts comma separated list
    --querytimeout: The timeout to use for the QueryTimeout reporter
    --gendata   : Generate data option. Passed to gentest.pl
    --seed      : PRNG seed. Passed to gentest.pl
    --mask      : Grammar mask. Passed to gentest.pl
    --mask-level: Grammar mask level. Passed to gentest.pl
    --rows      : No of rows. Passed to gentest.pl
    --varchar-length: length of strings. passed to gentest.pl
    --xml-output: Passed to gentest.pl
    --report-xml-tt: Passed to gentest.pl
    --report-xml-tt-type: Passed to gentest.pl
    --report-xml-tt-dest: Passed to gentest.pl
    --testname  : Name of test, used for reporting purposes
    --sqltrace  : Print all generated SQL statements.
                  Optional: Specify --sqltrace=MarkErrors to mark invalid statements.
    --views     : Generate views. Optionally specify view type (algorithm) as option value. Passed to gentest.pl
    --valgrind  : Passed to gentest.pl
    --filter    : Passed to gentest.pl
    --mem       : Passed to mtr
    --mtr-build-thread: Value used for MTR_BUILD_THREAD when servers are started and accessed 
    --short_column_names: Use short column names in gendata (c<number>)
    --strict_fields: Disable all AI applied to columns defined in \$fields in the gendata file. Allows for very specific column definitions
    --freeze_time: Freeze time for each query so that CURRENT_TIMESTAMP gives the same result for all transformers/validators
    --debug     : Debug mode
    --help      : This help message

    If you specify --basedir1 and --basedir2 or --vardir1 and --vardir2, two servers will be started and the results from the queries
    will be compared between them.
EOF
	;
	print "$0 arguments were: ".join(' ', @ARGV_saved)."\n";
	exit_test(STATUS_UNKNOWN_ERROR);
}

sub exit_test {
	my $status = shift;

	print isoTimestamp()." [$$] $0 will exit with exit status ".status2text($status)." ($status)\n";
	safe_exit($status);
}
