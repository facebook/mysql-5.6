#!/usr/bin/perl

# Copyright (c) 2010,2011 Oracle and/or its affiliates. All rights reserved.
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

#################### FOR THE MOMENT THIS SCRIPT IS FOR TESTING PURPOSES

use lib 'lib';
use lib "$ENV{RQG_HOME}/lib";
use Carp;
use strict;
use GenTest;
use GenTest::BzrInfo;
use GenTest::Constants;
use GenTest::Properties;
use GenTest::App::GenTest;
use DBServer::DBServer;
use DBServer::MySQL::MySQLd;
use DBServer::MySQL::ReplMySQLd;

my $logger;
eval
{
    require Log::Log4perl;
    Log::Log4perl->import();
    $logger = Log::Log4perl->get_logger('randgen.gentest');
};

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
my @dsns;

my ($gendata, @basedirs, @mysqld_options, @vardirs, $rpl_mode,
    $engine, $help, $debug, @validators, @reporters, @transformers, 
    $grammar_file, $skip_recursive_rules,
    $redefine_file, $seed, $mask, $mask_level, $mem, $rows,
    $varchar_len, $xml_output, $valgrind, @valgrind_options, $views,
    $start_dirty, $filter, $build_thread, $sqltrace, $testname,
    $report_xml_tt, $report_xml_tt_type, $report_xml_tt_dest,
    $notnull, $logfile, $logconf, $report_tt_logdir, $querytimeout, $no_mask,
    $short_column_names, $strict_fields, $freeze_time);

my $gendata=''; ## default simple gendata

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
	#'basedir=s@' => \@basedirs,
	'vardir=s' => \$vardirs[0],
	'vardir1=s' => \$vardirs[0],
	'vardir2=s' => \$vardirs[1],
	#'vardir=s@' => \@vardirs,
	'rpl_mode=s' => \$rpl_mode,
	'engine=s' => \$engine,
	'grammar=s' => \$grammar_file,
	'skip-recursive-rules' > \$skip_recursive_rules,
	'redefine=s' => \$redefine_file,
	'threads=i' => \$threads,
	'queries=s' => \$queries,
	'duration=i' => \$duration,
	'help' => \$help,
	'debug' => \$debug,
	'validators=s@' => \@validators,
	'reporters=s@' => \@reporters,
	'transformers=s@' => \@transformers,
	'gendata:s' => \$gendata,
	'notnull' => \$notnull,
	'short_column_names' => \$short_column_names,
    'freeze_time' => \$freeze_time,
	'strict_fields' => \$strict_fields,
	'seed=s' => \$seed,
	'mask=i' => \$mask,
    'mask-level=i' => \$mask_level,
	'mem' => \$mem,
	'rows=i' => \$rows,
	'varchar-length=i' => \$varchar_len,
	'xml-output=s'	=> \$xml_output,
	'report-xml-tt'	=> \$report_xml_tt,
	'report-xml-tt-type=s' => \$report_xml_tt_type,
	'report-xml-tt-dest=s' => \$report_xml_tt_dest,
	'testname=s'		=> \$testname,
	'valgrind!'	=> \$valgrind,
	'valgrind_options=s@'	=> \@valgrind_options,
	'views:s'		=> \$views,
	'start-dirty'	=> \$start_dirty,
	'filter=s'	=> \$filter,
    'mtr-build-thread=i' => \$build_thread,
    'sqltrace:s' => \$sqltrace,
    'logfile=s' => \$logfile,
    'logconf=s' => \$logconf,
    'report-tt-logdir=s' => \$report_tt_logdir,
    'querytimeout=i' => \$querytimeout,
    'no-mask' => \$no_mask
    );

if (defined $logfile && defined $logger) {
    setLoggingToFile($logfile);
} else {
    if (defined $logconf && defined $logger) {
        setLogConf($logconf);
    }
}

if (!$opt_result || $help || $basedirs[0] eq '' || not defined $grammar_file) {
	help();
	exit($help ? 0 : 1);
}

if (defined $sqltrace) {
    # --sqltrace may have a string value (optional). 
    # Allowed values for --sqltrace:
    my %sqltrace_legal_values = (
        'MarkErrors'    => 1  # Prefixes invalid SQL statements for easier post-processing
    );
    
    if (length($sqltrace) > 0) {
        # A value is given, check if it is legal.
        if (not exists $sqltrace_legal_values{$sqltrace}) {
            say("Invalid value for --sqltrace option: '$sqltrace'");
            say("Valid values are: ".join(', ', keys(%sqltrace_legal_values)));
            say("No value means that default/plain sqltrace will be used.");
            exit(STATUS_ENVIRONMENT_FAILURE);
        }
    } else {
        # If no value is given, GetOpt will assign the value '' (empty string).
        # We interpret this as plain tracing (no marking of errors, prefixing etc.).
        # Better to use 1 instead of empty string for comparisons later.
        $sqltrace = 1;
    }
}

say("Copyright (c) 2010,2011 Oracle and/or its affiliates. All rights reserved. Use is subject to license terms.");
say("Please see http://forge.mysql.com/wiki/Category:RandomQueryGenerator for more information on this test framework.");
say("Starting \n# $0 \\ \n# ".join(" \\ \n# ", @ARGV_saved));

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

my @ports = (10000 + 10 * $build_thread, 10000 + 10 * $build_thread + 2);

say("master_port : $ports[0] slave_port : $ports[1] ports : @ports MTR_BUILD_THREAD : $build_thread ");

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
# If the user has provided identical basedirs and vardirs, warn of a
# potential overlap.
#

if (
	($basedirs[0] eq $basedirs[1]) &&
	($vardirs[0] eq $vardirs[1]) &&
	($rpl_mode eq '')
    ) {
	croak("Please specify either different --basedir[12] or different --vardir[12] in order to start two MySQL servers");
}

my $client_basedir;

foreach my $path ("$basedirs[0]/client/RelWithDebInfo", "$basedirs[0]/client/Debug", "$basedirs[0]/client", "$basedirs[0]/bin") {
	if (-e $path) {
		$client_basedir = $path;
		last;
	}
}

#
# Start servers. Use rpl_alter if replication is needed.
#

my @server;
my $rplsrv;

if ($rpl_mode ne '') {
    my @options;
    push @options, lc("--$engine") if defined $engine && lc($engine) ne lc('myisam');
    
    push @options, "--sql-mode=no_engine_substitution" if join(' ', @ARGV_saved) !~ m{sql-mode}io;
    
    if (defined $mysqld_options[0]) {
        push @options, @{$mysqld_options[0]};
    }
    $rplsrv = DBServer::MySQL::ReplMySQLd->new(basedir => $basedirs[0],
                                               master_vardir => $vardirs[0],
                                               master_port => $ports[0],
                                               slave_vardir => $vardirs[1],
                                               slave_port => $ports[1],
                                               mode => $rpl_mode,
                                               server_options => \@options,
                                               valgrind => $valgrind,
                                               valgrind_options => \@valgrind_options,
                                               general_log => 1,
                                               start_dirty => $start_dirty);
    
    my $status = $rplsrv->startServer();
    
    if ($status > DBSTATUS_OK) {
        stopServers();
        if (osWindows()) {
            say(system("dir ".unix2winPath($rplsrv->master->datadir)));
            say(system("dir ".unix2winPath($rplsrv->slave->datadir)));
        } else {
            say(system("ls -l ".$rplsrv->master->datadir));
            say(system("ls -l ".$rplsrv->slave->datadir));
        }
        croak("Could not start replicating server pair");
    }
    
    $dsns[0] = $rplsrv->master->dsn($database);
    $dsns[1] = undef; ## passed to gentest. No dsn for slave!
    $server[0] = $rplsrv->master;
    $server[1] = $rplsrv->slave;
    
} else {
    if ($#basedirs != $#vardirs) {
        croak ("The number of basedirs and vardirs must match $#basedirs != $#vardirs")
    }
    foreach my $server_id (0..1) {
        next if $basedirs[$server_id] eq '';
        
        my @options;
        push @options, lc("--$engine") if defined $engine && lc($engine) ne lc('myisam');
        
        push @options, "--sql-mode=no_engine_substitution" if join(' ', @ARGV_saved) !~ m{sql-mode}io;
        
        if (defined $mysqld_options[$server_id]) {
            push @options, @{$mysqld_options[$server_id]};
        }
        $server[$server_id] = DBServer::MySQL::MySQLd->new(basedir => $basedirs[$server_id],
                                                           vardir => $vardirs[$server_id],
                                                           port => $ports[$server_id],
                                                           start_dirty => $start_dirty,
                                                           valgrind => $valgrind,
                                                           valgrind_options => \@valgrind_options,
                                                           server_options => \@options,
                                                           general_log => 1);
        
        my $status = $server[$server_id]->startServer;
        
        if ($status > DBSTATUS_OK) {
            stopServers();
            if (osWindows()) {
                say(system("dir ".unix2winPath($server[$server_id]->datadir)));
            } else {
                say(system("ls -l ".$server[$server_id]->datadir));
            }
            croak("Could not start all servers");
        }
        
        if (
            ($server_id == 0) ||
            ($rpl_mode eq '') 
            ) {
            $dsns[$server_id] = $server[$server_id]->dsn($database);
        }
    
        if ((defined $dsns[$server_id]) && (defined $engine)) {
            my $dbh = DBI->connect($dsns[$server_id], undef, undef, { RaiseError => 1 } );
            $dbh->do("SET GLOBAL storage_engine = '$engine'");
        }
    }
}

#
# Run actual queries
#

my $gentestProps = GenTest::Properties->new(
    legal => ['grammar',
              'skip-recursive-rules',
              'dsn',
              'engine',
              'gendata',
              'generator',
              'redefine',
              'threads',
              'queries',
              'duration',
              'help',
              'debug',
              'rpl_mode',
              'validators',
              'reporters',
              'reporters',
              'transformers',
              'seed',
              'mask',
              'mask-level',
              'rows',
              'varchar-length',
              'xml-output',
              'views',
              'start-dirty',
              'filter',
              'notnull',
              'short_column_names',
              'strict_fields',
              'freeze_time',
              'valgrind',
              'valgrind-xml',
              'testname',
              'sqltrace',
              'querytimeout',
              'report-xml-tt',
              'report-xml-tt-type',
              'report-xml-tt-dest',
              'logfile',
              'logconf',
              'report-tt-logdir']
    );

my @gentest_options;

## For backward compatability
if ($#validators == 0 and $validators[0] =~ m/,/) {
    @validators = split(/,/,$validators[0]);
}

## For backward compatability
if ($#reporters == 0 and $reporters[0] =~ m/,/) {
    @reporters = split(/,/,$reporters[0]);
}

## For backward compatability
if ($#transformers == 0 and $transformers[0] =~ m/,/) {
    @transformers = split(/,/,$transformers[0]);
}

$gentestProps->property('generator','FromGrammar') if not defined $gentestProps->property('generator');

$gentestProps->property('start-dirty',1) if defined $start_dirty;
$gentestProps->gendata($gendata);
$gentestProps->engine($engine) if defined $engine;
$gentestProps->rpl_mode($rpl_mode) if defined $rpl_mode;
$gentestProps->validators(\@validators) if @validators;
$gentestProps->reporters(\@reporters) if @reporters;
$gentestProps->transformers(\@transformers) if @transformers;
$gentestProps->threads($threads) if defined $threads;
$gentestProps->queries($queries) if defined $queries;
$gentestProps->duration($duration) if defined $duration;
$gentestProps->dsn(\@dsns) if @dsns;
$gentestProps->grammar($grammar_file);
$gentestProps->property('skip-recursive-rules', $skip_recursive_rules);
$gentestProps->redefine($redefine_file) if defined $redefine_file;
$gentestProps->seed($seed) if defined $seed;
$gentestProps->mask($mask) if (defined $mask) && (not defined $no_mask);
$gentestProps->property('mask-level',$mask_level) if defined $mask_level;
$gentestProps->rows($rows) if defined $rows;
if (defined $views) {
	if ($views eq '1') {
		$gentestProps->property('views', 'UNDEFINED');
	} else {
		$gentestProps->property('views', $views);
	}
}
$gentestProps->property('varchar-length',$varchar_len) if defined $varchar_len;
$gentestProps->property('xml-output',$xml_output) if defined $xml_output;
$gentestProps->debug(1) if defined $debug;
$gentestProps->filter($filter) if defined $filter;
$gentestProps->notnull($notnull) if defined $notnull;
$gentestProps->short_coulmn_names($short_column_names) if defined $short_column_names;
$gentestProps->strict_fields($strict_fields) if defined $strict_fields;
$gentestProps->freeze_time($freeze_time) if defined $freeze_time;
$gentestProps->valgrind(1) if $valgrind;
$gentestProps->sqltrace($sqltrace) if $sqltrace;
$gentestProps->querytimeout($querytimeout) if defined $querytimeout;
$gentestProps->testname($testname) if $testname;
$gentestProps->logfile($logfile) if defined $logfile;
$gentestProps->logconf($logconf) if defined $logconf;
$gentestProps->property('report-tt-logdir',$report_tt_logdir) if defined $report_tt_logdir;
$gentestProps->property('report-xml-tt', 1) if defined $report_xml_tt;
$gentestProps->property('report-xml-tt-type', $report_xml_tt_type) if defined $report_xml_tt_type;
$gentestProps->property('report-xml-tt-dest', $report_xml_tt_dest) if defined $report_xml_tt_dest;

# Push the number of "worker" threads into the environment.
# lib/GenTest/Generator/FromGrammar.pm will generate a corresponding grammar element.
$ENV{RQG_THREADS}= $threads;

my $gentest = GenTest::App::GenTest->new(config => $gentestProps);
my $gentest_result = $gentest->run();
say("GenTest exited with exit status ".status2text($gentest_result)." ($gentest_result)");
exit_test($gentest_result) if $gentest_result > 0;

#
# Compare master and slave, or two masters
#

if ($rpl_mode || (defined $basedirs[1])) {
    if ($rpl_mode ne '') {
        $rplsrv->waitForSlaveSync;
    }

    my @dump_files;
    
	foreach my $i (0..$#server) {
		$dump_files[$i] = tmpdir()."server_".$$."_".$i.".dump";
        
		my $dump_result = $server[$i]->dumpdb($database,$dump_files[$i]);
		exit_test($dump_result >> 8) if $dump_result > 0;
	}
    
	say("Comparing SQL dumps...");
	my $diff_result = system("diff -u $dump_files[0] $dump_files[1]");
	$diff_result = $diff_result >> 8;
    
	if ($diff_result == 0) {
		say("No differences were found between servers.");
	}
    
	foreach my $dump_file (@dump_files) {
		unlink($dump_file);
	}
    
	exit_test($diff_result);
}


stopServers();

sub stopServers {
    if ($rpl_mode ne '') {
        $rplsrv->stopServer();
    } else {
        foreach my $srv (@server) {
            if ($srv) {
                $srv->stopServer;
            }
        }
    }
}


sub help {
    
	print <<EOF
Copyright (c) 2010,2011 Oracle and/or its affiliates. All rights reserved. Use is subject to license terms.

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
    --notnull   : Generate all fields with NOT NULL
    --rows      : No of rows. Passed to gentest.pl
    --sqltrace  : Print all generated SQL statements. 
                  Optional: Specify --sqltrace=MarkErrors to mark invalid statements.
    --varchar-length: length of strings. passed to gentest.pl
    --xml-outputs: Passed to gentest.pl
    --views     : Generate views. Optionally specify view type (algorithm) as option value. Passed to gentest.pl
    --valgrind  : Passed to gentest.pl
    --filter    : Passed to gentest.pl
    --mem       : Passed to mtr
    --mtr-build-thread:  Value used for MTR_BUILD_THREAD when servers are started and accessed
    --debug     : Debug mode
    --short_column_names: use short column names in gendata (c<number>)
    --strict_fields: Disable all AI applied to columns defined in \$fields in the gendata file. Allows for very specific column definitions
    --freeze_time: Freeze time for each query so that CURRENT_TIMESTAMP gives the same result for all transformers/validators
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
    stopServers();
	say("[$$] $0 will exit with exit status ".status2text($status). " ($status)");
	safe_exit($status);
}
