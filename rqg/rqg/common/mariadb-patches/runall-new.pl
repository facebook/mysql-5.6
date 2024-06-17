#!/usr/bin/perl

# Copyright (c) 2010, 2012, Oracle and/or its affiliates. All rights reserved.
# Copyright (c) 2013, Monty Program Ab.
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
use GenTest::App::GenConfig;
use DBServer::DBServer;
use DBServer::MySQL::MySQLd;
use DBServer::MySQL::ReplMySQLd;
use DBServer::MySQL::GaleraMySQLd;

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
my $user = 'rqg';
my @dsns;

my ($gendata, @basedirs, @mysqld_options, @vardirs, $rpl_mode,
    $engine, $help, $debug, @validators, @reporters, @transformers, 
    $grammar_file, $skip_recursive_rules,
    @redefine_files, $seed, $mask, $mask_level, $mem, $rows,
    $varchar_len, $xml_output, $valgrind, @valgrind_options, @views,
    $start_dirty, $filter, $build_thread, $sqltrace, $testname,
    $report_xml_tt, $report_xml_tt_type, $report_xml_tt_dest,
    $notnull, $logfile, $logconf, $report_tt_logdir, $querytimeout, $no_mask,
    $short_column_names, $strict_fields, $freeze_time, $wait_debugger, @debug_server,
    $skip_gendata, $skip_shutdown, $galera, $use_gtid, $genconfig, $annotate_rules,
    $restart_timeout);

my $gendata=''; ## default simple gendata

my $genconfig=''; # if template is not set, the server will be run with --no-defaults

my $threads = my $default_threads = 10;
my $queries = my $default_queries = 1000;
my $duration = my $default_duration = 3600;

my @ARGV_saved = @ARGV;

my $opt_result = GetOptions(
	'mysqld=s@' => \$mysqld_options[0],
	'mysqld1=s@' => \$mysqld_options[1],
	'mysqld2=s@' => \$mysqld_options[2],
        'basedir=s' => \$basedirs[0],
        'basedir1=s' => \$basedirs[0],
        'basedir2=s' => \$basedirs[1],
	#'basedir=s@' => \@basedirs,
        'vardir=s' => \$vardirs[0],
        'vardir1=s' => \$vardirs[0],
        'vardir2=s' => \$vardirs[1],
        'debug-server' => \$debug_server[0],
        'debug-server1' => \$debug_server[0],
        'debug-server2' => \$debug_server[1],
	#'vardir=s@' => \@vardirs,
	'rpl_mode=s' => \$rpl_mode,
	'rpl-mode=s' => \$rpl_mode,
	'engine=s' => \$engine,
	'grammar=s' => \$grammar_file,
	'skip-recursive-rules' > \$skip_recursive_rules,
	'redefine=s@' => \@redefine_files,
	'threads=i' => \$threads,
	'queries=s' => \$queries,
	'duration=i' => \$duration,
	'help' => \$help,
	'debug' => \$debug,
	'validators=s@' => \@validators,
	'reporters=s@' => \@reporters,
	'transformers=s@' => \@transformers,
	'gendata:s' => \$gendata,
	'skip-gendata' => \$skip_gendata,
	'genconfig:s' => \$genconfig,
	'notnull' => \$notnull,
	'short_column_names' => \$short_column_names,
        'freeze_time' => \$freeze_time,
	'strict_fields' => \$strict_fields,
	'seed=s' => \$seed,
	'mask=i' => \$mask,
        'mask-level=i' => \$mask_level,
	'mem' => \$mem,
	'rows=s' => \$rows,
	'varchar-length=i' => \$varchar_len,
	'xml-output=s'	=> \$xml_output,
	'report-xml-tt'	=> \$report_xml_tt,
	'report-xml-tt-type=s' => \$report_xml_tt_type,
	'report-xml-tt-dest=s' => \$report_xml_tt_dest,
	'restart_timeout=i' => \$restart_timeout,
	'restart-timeout=i' => \$restart_timeout,
	'testname=s'		=> \$testname,
	'valgrind!'	=> \$valgrind,
	'valgrind_options=s@'	=> \@valgrind_options,
	'views:s'		=> \$views[0],
	'views1:s'		=> \$views[1],
	'views2:s'		=> \$views[2],
	'wait-for-debugger' => \$wait_debugger,
	'start-dirty'	=> \$start_dirty,
	'filter=s'	=> \$filter,
        'mtr-build-thread=i' => \$build_thread,
        'sqltrace:s' => \$sqltrace,
        'logfile=s' => \$logfile,
        'logconf=s' => \$logconf,
        'report-tt-logdir=s' => \$report_tt_logdir,
        'querytimeout=i' => \$querytimeout,
        'no-mask' => \$no_mask,
	'skip_shutdown' => \$skip_shutdown,
	'skip-shutdown' => \$skip_shutdown,
	'galera=s' => \$galera,
	'use-gtid=s' => \$use_gtid,
	'use_gtid=s' => \$use_gtid,
	'annotate_rules' => \$annotate_rules,
	'annotate-rules' => \$annotate_rules
    );

if (defined $logfile && defined $logger) {
    setLoggingToFile($logfile);
} else {
    if (defined $logconf && defined $logger) {
        setLogConf($logconf);
    }
}

if ($help) {
        help();
        exit 0;
}
if ($basedirs[0] eq '') {
	print STDERR "\nERROR: Basedir is not defined\n\n";
        help();
        exit 1;
}
if (not defined $grammar_file) {
	print STDERR "\nERROR: Grammar file is not defined\n\n";
        help();
        exit 1;
}
if (!$opt_result) {
	print STDERR "\nERROR: Error occured while reading options\n\n";
	help();
	exit 1;
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

# Originally it was done in Gendata, but we want the same seed for all components

if (defined $seed and $seed eq 'time') {
	$seed = time();
	say("Converted --seed=time to --seed=$seed");
}

my $cmd = $0 . " " . join(" ", @ARGV_saved);
$cmd =~ s/seed=time/seed=$seed/g;
say("Final command line: \nperl $cmd");


my $cnf_array_ref;

if ($genconfig) {
	unless (-e $genconfig) {
		croak("ERROR: Specified config template $genconfig does not exist");
	}
	$cnf_array_ref = GenTest::App::GenConfig->new(spec_file => $genconfig,
                                               seed => $seed,
                                               debug => $debug
	);
}

#
# Start servers. Use rpl_alter if replication is needed.
#

my @server;
my $rplsrv;

# mysqld_options[0] are those that are applied to all servers (--mysqld=...)
# mysqld_options[N] (N in 1,2) are those that are applied to the corresponding server only,
# if it's started (--mysqld1=...)


@{$mysqld_options[0]} = () if not defined $mysqld_options[0];
push @{$mysqld_options[0]}, lc("--$engine") if defined $engine && (lc($engine) ne lc('myisam') && lc($engine) ne lc('memory'));
push @{$mysqld_options[0]}, "--sql-mode=no_engine_substitution" if join(' ', @ARGV_saved) !~ m{sql-mode}io;

@{$mysqld_options[1]} = ( defined $mysqld_options[1] 
		? ( @{$mysqld_options[0]}, @{$mysqld_options[1]} )
		: @{$mysqld_options[0]}
);
    
@{$mysqld_options[2]} = ( defined $mysqld_options[2] 
		? ( @{$mysqld_options[0]}, @{$mysqld_options[2]} )
		: @{$mysqld_options[0]}
);

if ($rpl_mode ne '') {

    $rplsrv = DBServer::MySQL::ReplMySQLd->new(master_basedir => $basedirs[0],
                                               slave_basedir => $basedirs[1],
                                               master_vardir => $vardirs[0],
                                               debug_server => $debug_server[0],
                                               master_port => $ports[0],
                                               slave_vardir => $vardirs[1],
                                               slave_port => $ports[1],
                                               mode => $rpl_mode,
                                               server_options => $mysqld_options[1],
                                               valgrind => $valgrind,
                                               valgrind_options => \@valgrind_options,
                                               general_log => 1,
                                               start_dirty => $start_dirty,
                                               use_gtid => $use_gtid,
                                               config => $cnf_array_ref,
                                               user => $user
	);
    
    my $status = $rplsrv->startServer();
    
    if ($status > DBSTATUS_OK) {
        stopServers($status);
        if (osWindows()) {
            say(system("dir ".unix2winPath($rplsrv->master->datadir)));
            say(system("dir ".unix2winPath($rplsrv->slave->datadir)));
        } else {
            say(system("ls -l ".$rplsrv->master->datadir));
            say(system("ls -l ".$rplsrv->slave->datadir));
        }
        croak("Could not start replicating server pair");
    }
    
    $dsns[0] = $rplsrv->master->dsn($database,$user);
    $dsns[1] = undef; ## passed to gentest. No dsn for slave!
    $server[0] = $rplsrv->master;
    $server[1] = $rplsrv->slave;

} elsif ($galera ne '') {

	if (osWindows()) {
		croak("Galera is not supported on Windows (yet)");
	}

	unless ($galera =~ /^[ms]+$/i) {
		croak ("--galera option should contain a combination of M and S, indicating masters and slaves");
	}

	$rplsrv = DBServer::MySQL::GaleraMySQLd->new(
		basedir => $basedirs[0],
		parent_vardir => $vardirs[0],
		debug_server => $debug_server[0],
		first_port => $ports[0],
		server_options => $mysqld_options[1],
		valgrind => $valgrind,
		valgrind_options => \@valgrind_options,
		general_log => 1,
		start_dirty => $start_dirty,
		node_count => length($galera)
	);
    
	my $status = $rplsrv->startServer();
    
	if ($status > DBSTATUS_OK) {
		stopServers($status);

		say("ERROR: Could not start Galera cluster");
		exit_test(STATUS_ENVIRONMENT_FAILURE);
	}

	my $galera_topology = $galera;
	my $i = 0;
	while ($galera_topology =~ s/^(\w)//) {
		if (lc($1) eq 'm') {
			$dsns[$i] = $rplsrv->nodes->[$i]->dsn($database,$user);
		}
		$server[$i] = $rplsrv->nodes->[$i];
		$i++;
	}

} else {
    if ($#basedirs != $#vardirs) {
        croak ("The number of basedirs and vardirs must match $#basedirs != $#vardirs")
    }
    foreach my $server_id (0..1) {
        next if $basedirs[$server_id] eq '';
        
        $server[$server_id] = DBServer::MySQL::MySQLd->new(basedir => $basedirs[$server_id],
                                                           vardir => $vardirs[$server_id],
                                                           debug_server => $debug_server[$server_id],
                                                           port => $ports[$server_id],
                                                           start_dirty => $start_dirty,
                                                           valgrind => $valgrind,
                                                           valgrind_options => \@valgrind_options,
                                                           server_options => $mysqld_options[$server_id+1],
                                                           general_log => 1,
                                                           config => $cnf_array_ref,
                                                           user => $user);
        
        my $status = $server[$server_id]->startServer;
        
        if ($status > DBSTATUS_OK) {
            stopServers($status);
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
            $dsns[$server_id] = $server[$server_id]->dsn($database,$user);
        }
    
        if ((defined $dsns[$server_id]) && (defined $engine)) {
            my $dbh = DBI->connect($dsns[$server_id], undef, undef, { mysql_multi_statements => 1, RaiseError => 1 } );
            $dbh->do("SET GLOBAL default_storage_engine = '$engine'");
        }
    }
}


#
# Wait for user interaction before continuing, allowing the user to attach 
# a debugger to the server process(es).
# Will print a message and ask the user to press a key to continue.
# User is responsible for actually attaching the debugger if so desired.
#
if ($wait_debugger) {
    say("Pausing test to allow attaching debuggers etc. to the server process.");
    my @pids;   # there may be more than one server process
    foreach my $server_id (0..$#server) {
        $pids[$server_id] = $server[$server_id]->serverpid;
    }
    say('Number of servers started: '.($#server+1));
    say('Server PID: '.join(', ', @pids));
    say("Press ENTER to continue the test run...");
    my $keypress = <STDIN>;
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
              'debug_server',
              'report-tt-logdir',
              'servers',
              'multi-master',
              'annotate-rules',
              'restart-timeout'
]
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

## For backward compatibility

# if --views[=<value>] is defined, it will be used for both servers.
# --views1 and --views2 will only be used for the corresponding server 
#   and have priority over --views

if (defined $views[0]) {
	$views[1] = $views[0] unless defined $views[1];
	$views[2] = $views[0] unless defined $views[2];
}
shift @views;


## For uniformity
if ($#redefine_files == 0 and $redefine_files[0] =~ m/,/) {
    @redefine_files = split(/,/,$redefine_files[0]);
}

$gentestProps->property('generator','FromGrammar') if not defined $gentestProps->property('generator');

$gentestProps->property('start-dirty',1) if defined $start_dirty;
$gentestProps->gendata($gendata) unless defined $skip_gendata;
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
$gentestProps->redefine(\@redefine_files) if @redefine_files;
$gentestProps->seed($seed) if defined $seed;
$gentestProps->mask($mask) if (defined $mask) && (not defined $no_mask);
$gentestProps->property('mask-level',$mask_level) if defined $mask_level;
$gentestProps->rows($rows) if defined $rows;
$gentestProps->views(\@views) if @views;
$gentestProps->property('varchar-length',$varchar_len) if defined $varchar_len;
$gentestProps->property('xml-output',$xml_output) if defined $xml_output;
$gentestProps->debug(1) if defined $debug;
$gentestProps->filter($filter) if defined $filter;
$gentestProps->notnull($notnull) if defined $notnull;
$gentestProps->short_column_names($short_column_names) if defined $short_column_names;
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
$gentestProps->property('restart-timeout', $restart_timeout) if defined $restart_timeout;
# In case of multi-master topology (e.g. Galera with multiple "masters"),
# we don't want to compare results after each query.
# Instead, we want to run the flow independently and only compare dumps at the end.
# If GenTest gets 'multi-master' property, it won't run ResultsetComparator
$gentestProps->property('multi-master', 1) if (defined $galera and scalar(@dsns)>1);
# Pass debug server if used.
$gentestProps->debug_server(\@debug_server) if @debug_server;
$gentestProps->servers(\@server) if @server;
$gentestProps->property('annotate-rules',$annotate_rules) if defined $annotate_rules;


# Push the number of "worker" threads into the environment.
# lib/GenTest/Generator/FromGrammar.pm will generate a corresponding grammar element.
$ENV{RQG_THREADS}= $threads;

my $gentest = GenTest::App::GenTest->new(config => $gentestProps);
my $gentest_result = $gentest->run();
say("GenTest exited with exit status ".status2text($gentest_result)." ($gentest_result)");

# If Gentest produced any failure then exit with its failure code,
# otherwise if the test is replication/with two servers compare the 
# server dumps for any differences else if there are no failures exit with success.

if (($gentest_result == STATUS_OK) && ($rpl_mode || (defined $basedirs[1]) || $galera)) {
#
# Compare master and slave, or all masters
#
	my $diff_result;
	if ($rpl_mode ne '') {
		$diff_result = $rplsrv->waitForSlaveSync;
		if ($diff_result != STATUS_OK) {
			exit_test(STATUS_INTERNAL_ERROR);
		}
	}
  
	my @dump_files;
  
	foreach my $i (0..$#server) {
		$dump_files[$i] = tmpdir()."server_".abs($$)."_".$i.".dump";
      
		my $dump_result = $server[$i]->dumpdb($database,$dump_files[$i]);
		exit_test($dump_result >> 8) if $dump_result > 0;
	}
  
	say("Comparing SQL dumps...");
	$diff_result = system("diff -u $dump_files[0] $dump_files[1]");
	$diff_result = ($diff_result ? STATUS_CONTENT_MISMATCH : STATUS_OK);
  
	if ($diff_result == STATUS_OK) {
		say("No differences were found between servers.");
	}
  
	foreach my $dump_file (@dump_files) {
		unlink($dump_file);
	}
	exit_test($diff_result);
} else {
	# If test was not sucessfull or not rpl/multiple servers.
	exit_test($gentest_result);
}

sub stopServers {
    my $status = shift;
    if ($skip_shutdown) {
        say("Server shutdown is skipped upon request");
        return;
    }
    say("Stopping server(s)...");
    if ($rpl_mode ne '') {
        $rplsrv->stopServer($status);
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
    --debug-server: Use mysqld-debug server

    Options related to two MySQL servers

    --basedir1  : Specifies the base directory of the first MySQL installation;
    --basedir2  : Specifies the base directory of the second MySQL installation;
    --mysqld    : Options passed to both MySQL servers
    --mysqld1   : Options passed to the first MySQL server
    --mysqld2   : Options passed to the second MySQL server
    --debug-server1: Use mysqld-debug server for MySQL server1
    --debug-server2: Use mysqld-debug server for MySQL server2
    --vardir1   : Optional. (default \$basedir1/mysql-test/var);
    --vardir2   : Optional. (default \$basedir2/mysql-test/var);

    General options

    --grammar   : Grammar file to use when generating queries (REQUIRED);
    --redefine  : Grammar file(s) to redefine and/or add rules to the given grammar
    --rpl_mode  : Replication type to use (statement|row|mixed) (default: no replication);
    --use_gtid  : Use GTID mode for replication (current_pos|slave_pos|no). Adds the MASTER_USE_GTID clause to CHANGE MASTER,
                  (default: empty, no additional clause in CHANGE MASTER command);
    --galera    : Galera topology, presented as a string of 'm' or 's' (master or slave).
                  The test flow will be executed on each "master". "Slaves" will only be updated through Galera replication
    --engine    : Table engine to use when creating tables with gendata (default no ENGINE in CREATE TABLE);
    --threads   : Number of threads to spawn (default $default_threads);
    --queries   : Number of queries to execute per thread (default $default_queries);
    --duration  : Duration of the test in seconds (default $default_duration seconds);
    --validator : The validators to use
    --reporter  : The reporters to use
    --transformer: The transformers to use (turns on --validator=transformer). Accepts comma separated list
    --querytimeout: The timeout to use for the QueryTimeout reporter 
    --gendata   : Generate data option. Passed to gentest.pl
    --logfile   : Generates rqg output log at the path specified.(Requires the module Log4Perl)
    --seed      : PRNG seed. Passed to gentest.pl
    --mask      : Grammar mask. Passed to gentest.pl
    --mask-level: Grammar mask level. Passed to gentest.pl
    --notnull   : Generate all fields with NOT NULL
    --rows      : No of rows. Passed to gentest.pl
    --sqltrace  : Print all generated SQL statements. 
                  Optional: Specify --sqltrace=MarkErrors to mark invalid statements.
    --varchar-length: length of strings. passed to gentest.pl
    --xml-outputs: Passed to gentest.pl
    --views     : Generate views. Optionally specify view type (algorithm) as option value. Passed to gentest.pl.
                  Different values can be provided to two servers through --views1 | --views2
    --valgrind  : Passed to gentest.pl
    --filter    : Passed to gentest.pl
    --mem       : Passed to mtr
    --mtr-build-thread:  Value used for MTR_BUILD_THREAD when servers are started and accessed
    --debug     : Debug mode
    --short_column_names: use short column names in gendata (c<number>)
    --strict_fields: Disable all AI applied to columns defined in \$fields in the gendata file. Allows for very specific column definitions
    --freeze_time: Freeze time for each query so that CURRENT_TIMESTAMP gives the same result for all transformers/validators
    --annotate-rules: Add to the resulting query a comment with the rule name before expanding each rule. 
                      Useful for debugging query generation, otherwise makes the query look ugly and barely readable.
    --wait-for-debugger: Pause and wait for keypress after server startup to allow attaching a debugger to the server process.
    --restart-timeout: If the server has gone away, do not fail immediately, but wait to see if it restarts (it might be a part of the test)
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
    stopServers($status);
	say("[$$] $0 will exit with exit status ".status2text($status). " ($status)");
	safe_exit($status);
}
