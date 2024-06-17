#!/usr/bin/perl

# Copyright (c) 2008,2012 Oracle and/or its affiliates. All rights reserved.
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

package GenTest::App::GenTest;

@ISA = qw(GenTest);

use strict;
use Carp;
use Data::Dumper;
use File::Basename;
use File::Path 'mkpath';
use File::Copy;
use File::Spec;

use GenTest;
use GenTest::Properties;
use GenTest::Constants;
use GenTest::App::Gendata;
use GenTest::App::GendataSimple;
use GenTest::IPC::Channel;
use GenTest::IPC::Process;
use GenTest::ErrorFilter;
use GenTest::Grammar;

use POSIX;
use Time::HiRes;

use GenTest::XML::Report;
use GenTest::XML::Test;
use GenTest::XML::BuildInfo;
use GenTest::XML::Transporter;
use GenTest::Constants;
use GenTest::Result;
use GenTest::Validator;
use GenTest::Executor;
use GenTest::Mixer;
use GenTest::Reporter;
use GenTest::ReporterManager;
use GenTest::Filter::Regexp;
use GenTest::Incident;

use constant PROCESS_TYPE_PARENT	=> 0;
use constant PROCESS_TYPE_PERIODIC	=> 1;
use constant PROCESS_TYPE_CHILD		=> 2;

use constant GT_CONFIG => 0;
use constant GT_XML_TEST => 3;
use constant GT_XML_REPORT => 4;
use constant GT_CHANNEL => 5;

use constant GT_GRAMMAR => 6;
use constant GT_GENERATOR => 7;
use constant GT_REPORTER_MANAGER => 8;
use constant GT_TEST_START => 9;
use constant GT_TEST_END => 10;
use constant GT_QUERY_FILTERS => 11;
use constant GT_LOG_FILES_TO_REPORT => 12;

sub new {
    my $class = shift;
    
    my $self = $class->SUPER::new({
        'config' => GT_CONFIG},@_);
    
    croak ("Need config") if not defined $self->config;

    return $self;
}

sub config {
    return $_[0]->[GT_CONFIG];
}

sub grammar {
    return $_[0]->[GT_GRAMMAR];
}

sub generator {
    return $_[0]->[GT_GENERATOR];
}

sub XMLTest {
    return $_[0]->[GT_XML_TEST];
}

sub XMLReport {
    return $_[0]->[GT_XML_REPORT];
}

sub channel {
    return $_[0]->[GT_CHANNEL];
}

sub reporterManager {
    return $_[0]->[GT_REPORTER_MANAGER];
}

sub queryFilters {
    return $_[0]->[GT_QUERY_FILTERS];
}

sub logFilesToReport {
    return @{$_[0]->[GT_LOG_FILES_TO_REPORT]};
}

sub run {
    my $self = shift;

    $SIG{TERM} = sub { exit(0) };
    $SIG{CHLD} = "IGNORE" if osWindows();
    $SIG{INT} = "IGNORE";
    
    if (defined $ENV{RQG_HOME}) {
        $ENV{RQG_HOME} = osWindows() ? $ENV{RQG_HOME}.'\\' : $ENV{RQG_HOME}.'/';
    }
    
    $ENV{RQG_DEBUG} = 1 if $self->config->debug;

    $self->initSeed();

    my $queries = $self->config->queries;
    $queries =~ s{K}{000}so;
    $queries =~ s{M}{000000}so;
    $self->config->property('queries', $queries);

    say("-------------------------------\nConfiguration");
    $self->config->printProps;

    my $gendata_result = $self->doGenData();
    return $gendata_result if $gendata_result != STATUS_OK;

    $self->[GT_TEST_START] = time();
    $self->[GT_TEST_END] = $self->[GT_TEST_START] + $self->config->duration;

    $self->[GT_CHANNEL] = GenTest::IPC::Channel->new();

    my $init_generator_result = $self->initGenerator();
    return $init_generator_result if $init_generator_result != STATUS_OK;

    my $init_reporters_result = $self->initReporters();
    return $init_reporters_result if $init_reporters_result != STATUS_OK;

    my $init_validators_result = $self->initValidators();
    return $init_validators_result if $init_validators_result != STATUS_OK;

    # Cache metadata and other info that may be needed later
    my @log_files_to_report;
    foreach my $i (0..2) {
        next if $self->config->dsn->[$i] eq '';
        next if $self->config->dsn->[$i] !~ m{mysql}sio;
        my $metadata_executor = GenTest::Executor->newFromDSN($self->config->dsn->[$i], osWindows() ? undef : $self->channel());
        $metadata_executor->init();
        $metadata_executor->cacheMetaData() if defined $metadata_executor->dbh();
        
        # Cache log file names needed for result reporting at end-of-test
        
        # We do not copy the general log, as it may grow very large for some tests.
        #my $logfile_result = $metadata_executor->execute("SHOW VARIABLES LIKE 'general_log_file'");
        #push(@log_files_to_report, $logfile_result->data()->[0]->[1]);
        
        # Guessing the error log file name relative to datadir (lacking safer methods).
        my $datadir_result = $metadata_executor->execute("SHOW VARIABLES LIKE 'datadir'");
        my $errorlog;
        foreach my $errorlog_path (
            "../log/master.err",  # MTRv1 regular layout
            "../log/mysqld1.err", # MTRv2 regular layout
            "../mysql.err"        # DBServer::MySQL layout
        ) {
            my $possible_path = File::Spec->catfile($datadir_result->data()->[0]->[1], $errorlog_path);
            if (-e $possible_path) {
                $errorlog = $possible_path;
                last;
            }
        }
        push(@log_files_to_report, $errorlog) if defined $errorlog;
        
        $metadata_executor->disconnect();
        undef $metadata_executor;
    }

    $self->[GT_LOG_FILES_TO_REPORT] = \@log_files_to_report;

    if (defined $self->config->filter) {
       $self->[GT_QUERY_FILTERS] = [ GenTest::Filter::Regexp->new(
           file => $self->config->filter
       ) ];
    }
    
    say("Starting ".$self->config->threads." processes, ".
        $self->config->queries." queries each, duration ".
        $self->config->duration." seconds.");

    $self->initXMLReport();
    
    ### Start central reporting thread ####
    
    my $errorfilter = GenTest::ErrorFilter->new(channel => $self->channel());
    my $errorfilter_p = GenTest::IPC::Process->new(object => $errorfilter);
    if (!osWindows()) {
        $errorfilter_p->start($self->config->servers);
    }

    my $reporter_pid = $self->reportingProcess();
    
    ### Start worker children ###

    my %worker_pids;

    if ($self->config->threads > 0) {
        foreach my $worker_id (1..$self->config->threads) {
            my $worker_pid = $self->workerProcess($worker_id);
            $worker_pids{$worker_pid} = 1;
            Time::HiRes::sleep(0.1); # fork slowly for more predictability
        }
    }

    ### Main process
        
    if (osWindows()) {
        ## Important that this is done here in the parent after the last
        ## fork since on windows Process.pm uses threads
        $errorfilter_p->start();
    }

    # We are the parent process, wait for for all spawned processes to terminate
    my $total_status = STATUS_OK;
    my $reporter_died = 0;
        
    ## Parent thread does not use channel
    $self->channel()->close;

    # Worker & Reporter processes that were spawned.
    my @spawned_pids = (keys %worker_pids, $reporter_pid);
    
    OUTER: while (1) {
        # Wait for processes to complete, i.e only processes spawned by workers & reporters. 
        foreach my $spawned_pid (@spawned_pids) {
            my $child_pid = waitpid($spawned_pid, 0);
            my $child_exit_status = $? > 0 ? ($? >> 8) : 0;
            
            $total_status = $child_exit_status if $child_exit_status > $total_status;
            
            if ($child_pid == $reporter_pid) {
                $reporter_died = 1;
                last OUTER;
            } else {
                delete $worker_pids{$child_pid};
            }
            
            say("Process with pid $child_pid ended with status ".status2text($child_exit_status));
            last OUTER if $child_exit_status >= STATUS_CRITICAL_FAILURE;
            last OUTER if keys %worker_pids == 0;
            last OUTER if $child_pid == -1;
        }
    }

    foreach my $worker_pid (keys %worker_pids) {
        say("Killing remaining worker process with pid $worker_pid...");
        kill(15, $worker_pid);
    }
        
    if ($reporter_died == 0) {
        # Wait for periodic process to return the status of its last execution
        Time::HiRes::sleep(1);
        say("Killing periodic reporting process with pid $reporter_pid...");
        kill(15, $reporter_pid);
            
        if (osWindows()) {
            # We use sleep() + non-blocking waitpid() due to a bug in ActiveState Perl
            Time::HiRes::sleep(1);
            waitpid($reporter_pid, &POSIX::WNOHANG() );
        } else {
            waitpid($reporter_pid, 0);
        }
            
        if ($? > -1 ) {
            my $reporter_status = $? > 0 ? $? >> 8 : 0;
            say("For pid $reporter_pid reporter status " . status2text($reporter_status));
            $total_status = $reporter_status if $reporter_status > $total_status;
        }
    }
        
    $errorfilter_p->kill();

    return $self->reportResults($total_status);

}

sub reportResults {
    my ($self, $total_status) = @_;
      
    my $reporter_manager = $self->reporterManager();
    my @report_results;
        
    # New report type REPORTER_TYPE_END, used with reporter's that processes information at the end of a test.
    if ($total_status == STATUS_OK) {
        @report_results = $reporter_manager->report(REPORTER_TYPE_SUCCESS | REPORTER_TYPE_ALWAYS | REPORTER_TYPE_END);
    } elsif (
        ($total_status == STATUS_LENGTH_MISMATCH) ||
        ($total_status == STATUS_CONTENT_MISMATCH)
    ) {
        @report_results = $reporter_manager->report(REPORTER_TYPE_DATA | REPORTER_TYPE_ALWAYS | REPORTER_TYPE_END);
    } elsif ($total_status == STATUS_SERVER_CRASHED) {
        say("Server crash reported, initiating post-crash analysis...");
        @report_results = $reporter_manager->report(REPORTER_TYPE_CRASH | REPORTER_TYPE_ALWAYS);
    } elsif ($total_status == STATUS_SERVER_DEADLOCKED) {
        say("Server deadlock reported, initiating analysis...");
        @report_results = $reporter_manager->report(REPORTER_TYPE_DEADLOCK | REPORTER_TYPE_ALWAYS | REPORTER_TYPE_END);
    } elsif ($total_status == STATUS_SERVER_KILLED) {
        @report_results = $reporter_manager->report(REPORTER_TYPE_SERVER_KILLED | REPORTER_TYPE_ALWAYS | REPORTER_TYPE_END);
    } else {
        @report_results = $reporter_manager->report(REPORTER_TYPE_ALWAYS | REPORTER_TYPE_END);
    }
        
    my $report_status = shift @report_results;
    $total_status = $report_status if $report_status > $total_status;
    $total_status = STATUS_OK if $total_status == STATUS_SERVER_KILLED;

    $self->reportXMLIncidents($total_status, \@report_results);
        
    if ($total_status == STATUS_OK) {
        say("Test completed successfully.");
        return STATUS_OK;
    } else {
        say("Test completed with failure status ".status2text($total_status)." ($total_status)");
        return $total_status;
    }
}

sub stopChild {
    my ($self, $status) = @_;

    say("GenTest: child is being stopped with status " . status2text($status));
    croak "calling stopChild() without a \$status" if not defined $status;

    if (osWindows()) {
        exit $status;
    } else {
        safe_exit($status);
    }
}

sub reportingProcess {
    my $self = shift;

    my $reporter_pid = fork();

    if ($reporter_pid != 0) {
        return $reporter_pid;
    }

    my $reporter_killed = 0;
    local $SIG{TERM} = sub { $reporter_killed = 1 };

    ## Reporter process does not use channel
    $self->channel()->close();

    Time::HiRes::sleep(($self->config->threads + 1) / 10);
    say("Started periodic reporting process...");

    while (1) {
        my $reporter_status = $self->reporterManager()->monitor(REPORTER_TYPE_PERIODIC);
        $self->stopChild($reporter_status) if $reporter_status > STATUS_CRITICAL_FAILURE;
        last if $reporter_killed == 1;
        sleep(10);
    }

    $self->stopChild(STATUS_OK);
}

sub workerProcess {
    my ($self, $worker_id) = @_;

    my $worker_pid = fork();
    $self->channel()->writer;

    if ($worker_pid != 0) {
        return $worker_pid;
    }

    $| = 1;
    my $ctrl_c = 0;
    local $SIG{INT} = sub { $ctrl_c = 1 };

    $self->generator()->setSeed($self->config->seed() + $worker_id);
    $self->generator()->setThreadId($worker_id);

    my @executors;
    foreach my $i (0..2) {
        next if $self->config->dsn->[$i] eq '';
        my $executor = GenTest::Executor->newFromDSN($self->config->dsn->[$i], osWindows() ? undef : $self->channel());
        $executor->sqltrace($self->config->sqltrace);
        $executor->setId($i+1);
        push @executors, $executor;
    }

    my $mixer = GenTest::Mixer->new(
        generator => $self->generator(),
        executors => \@executors,
        validators => $self->config->validators,
        properties =>  $self->config,
        filters => $self->queryFilters(),
        end_time => $self->[GT_TEST_END],
        restart_timeout => $self->config->property('restart-timeout')
    );

    if (not defined $mixer) {
        say("GenTest: ERROR: Failed to create a Mixer, status will be set to ENVIRONMENT_FAILURE");
        $self->stopChild(STATUS_ENVIRONMENT_FAILURE);
    }
        
    my $worker_result = 0;

    foreach my $i (1..$self->config->queries) {
        my $query_result = $mixer->next();
        $worker_result = $query_result if $query_result > $worker_result && $query_result > STATUS_TEST_FAILURE;

        if ($query_result > STATUS_CRITICAL_FAILURE) {
				say("GenTest: Server crash or critical failure (". status2text($query_result) . ") reported, the child will be stopped");
            undef $mixer;	# so that destructors are called
            $self->stopChild($query_result);
        }

        last if $query_result == STATUS_EOF;
        last if $ctrl_c == 1;
        last if time() > $self->[GT_TEST_END];
    }
        
    foreach my $executor (@executors) {
        $executor->disconnect;
        undef $executor;
    }

    # Forcefully deallocate the Mixer so that Validator destructors are called
    undef $mixer;
    undef $self->[GT_QUERY_FILTERS];
        
    if ($worker_result > 0) {
        say("GenTest: Child worker process completed with error code $worker_result.");
        $self->stopChild($worker_result);
    } else {
        say("GenTest: Child worker process completed successfully.");
        $self->stopChild(STATUS_OK);
    }
}

sub doGenData {
    my $self = shift;

    return STATUS_OK if not defined $self->config->gendata();
    return STATUS_OK if defined $self->config->property('start-dirty');

    my $i = -1;
    foreach my $dsn (@{$self->config->dsn}) {
        $i++;
        next if $dsn eq '';
        my $gendata_result;
        if ($self->config->gendata eq '') {
            $gendata_result = GenTest::App::GendataSimple->new(
               dsn => $dsn,
               views => ${$self->config->views}[$i],
               engine => $self->config->engine,
               sqltrace=> $self->config->sqltrace,
               notnull => $self->config->notnull,
               rows => $self->config->rows,
               varchar_length => $self->config->property('varchar-length')
            )->run();
        } else {
            $gendata_result = GenTest::App::Gendata->new(
               spec_file => $self->config->gendata,
               dsn => $dsn,
               engine => $self->config->engine,
               seed => $self->config->seed(),
               debug => $self->config->debug,
               rows => $self->config->rows,
               views => ${$self->config->views}[$i],
               varchar_length => $self->config->property('varchar-length'),
               sqltrace => $self->config->sqltrace,
               short_column_names => $self->config->short_column_names,
               strict_fields => $self->config->strict_fields,
               notnull => $self->config->notnull
            )->run();
        }
            
        return $gendata_result if $gendata_result > STATUS_OK;

        # For multi-master setup, e.g. Galera, we only need to do generatoion once
        return STATUS_OK if $self->config->property('multi-master');
    }

    return STATUS_OK;
}

sub initSeed {
    my $self = shift;
 
    return if not defined $self->config->seed();

    my $orig_seed = $self->config->seed();
    my $new_seed;

    if ($orig_seed eq 'time') {
        $new_seed = time();
    } elsif ($self->config->seed() eq 'epoch5') {
        $new_seed = time() % 100000;
    } elsif ($self->config->seed() eq 'random') {
        $new_seed = int(rand(32767));
    } else {
        $new_seed = $orig_seed;
    }

    if ($new_seed ne $orig_seed) {
        say("Converting --seed=$orig_seed to --seed=$new_seed");
        $self->config->property('seed', $new_seed);
    }
}

sub initGenerator {
    my $self = shift;

    my $generator_name = "GenTest::Generator::".$self->config->generator;
    say("Loading Generator $generator_name.") if rqg_debug();
    eval("use $generator_name");
    croak($@) if $@;

    if ($generator_name eq 'GenTest::Generator::FromGrammar') {
        if (not defined $self->config->grammar) {
            say("ERROR: --grammar not specified but Generator is $generator_name, status will be set to ENVIRONMENT_FAILURE");
            return STATUS_ENVIRONMENT_FAILURE;
        }

        $self->[GT_GRAMMAR] = GenTest::Grammar->new(
            grammar_file => $self->config->grammar,
            grammar_flags => (defined $self->config->property('skip-recursive-rules') ? GRAMMAR_FLAG_SKIP_RECURSIVE_RULES : undef )
        ) if defined $self->config->grammar;

        if (not defined $self->grammar()) {
            say("ERROR: Could not initialize the grammar, status will be set to ENVIRONMENT_FAILURE");
            return STATUS_ENVIRONMENT_FAILURE;
        }

        if ($self->config->redefine) {
            foreach (@{$self->config->redefine}) {
	             $self->[GT_GRAMMAR] = $self->[GT_GRAMMAR]->patch(
                    GenTest::Grammar->new( grammar_file => $_ )
                ) 
            }
        }

        if (not defined $self->grammar()) {
            say("ERROR: Could not redefine the grammar, status will be set to ENVIRONMENT_FAILURE");
            return STATUS_ENVIRONMENT_FAILURE;
        }
    }

    $self->[GT_GENERATOR] = $generator_name->new(
        grammar => $self->grammar(),
        varchar_length => $self->config->property('varchar-length'),
        mask => $self->config->mask,
        mask_level => $self->config->property('mask-level'),
        annotate_rules => $self->config->property('annotate-rules')
    );

    if (not defined $self->generator()) {
        say("ERROR: Could not initialize the generator, status will be set to ENVIRONMENT_FAILURE");
        return STATUS_ENVIRONMENT_FAILURE;
    }
}

sub isMySQLCompatible {
    my $self = shift;

    my $is_mysql_compatible = 1;

    foreach my $i (0..2) {
        next if $self->config->dsn->[$i] eq '';
        $is_mysql_compatible = 0 if ($self->config->dsn->[$i] !~ m{mysql|drizzle}sio);
    }

    return $is_mysql_compatible;
}

sub initReporters {
    my $self = shift;

    if (not defined $self->config->reporters or $#{$self->config->reporters} < 0) {
        $self->config->reporters([]);
        if ($self->isMySQLCompatible()) {
            $self->config->reporters(['ErrorLog', 'Backtrace']);
            push @{$self->config->reporters}, 'ValgrindXMLErrors' if (defined $self->config->property('valgrind-xml'));
            push @{$self->config->reporters}, 'ReplicationConsistency' if $self->config->rpl_mode ne '';
        }
    } else {
        ## Remove the "None" reporter
        foreach my $i (0..$#{$self->config->reporters}) {
            delete $self->config->reporters->[$i] 
                if $self->config->reporters->[$i] eq "None" 
                or $self->config->reporters->[$i] eq '';
        }
    }

    say("Reporters: ".($#{$self->config->reporters} > -1 ? join(', ', @{$self->config->reporters}) : "(none)"));
    
    my $reporter_manager = GenTest::ReporterManager->new();
    
    # pass option debug server to the reporter, for detecting the binary type.
    foreach my $i (0..2) {
        next if $self->config->dsn->[$i] eq '';
        foreach my $reporter (@{$self->config->reporters}) {
            my $add_result = $reporter_manager->addReporter($reporter, {
                dsn => $self->config->dsn->[$i],
                test_start => $self->[GT_TEST_START],
                test_end => $self->[GT_TEST_END],
                test_duration => $self->config->duration,
                debug_server => $self->config->debug_server->[$i],
                properties => $self->config
            });

            return $add_result if $add_result > STATUS_OK;
        }
    }

    $self->[GT_REPORTER_MANAGER] = $reporter_manager;
    return STATUS_OK;
}

sub initValidators {
    my $self = shift;

    if (not defined $self->config->validators or $#{$self->config->validators} < 0) {
        $self->config->validators([]);

        # In case of multi-master topology (e.g. Galera with multiple "masters"),
        # we don't want to compare results after each query.

        unless ($self->config->property('multi-master')) {
            if ($self->config->dsn->[2] ne '') {
                push @{$self->config->validators}, 'ResultsetComparator3';
            } elsif ($self->config->dsn->[1] ne '') {
                push @{$self->config->validators}, 'ResultsetComparator';
            }
        }        
        push @{$self->config->validators}, 'ReplicationSlaveStatus' 
            if $self->config->rpl_mode ne '' && $self->isMySQLCompatible();
        push @{$self->config->validators}, 'MarkErrorLog' 
            if (defined $self->config->valgrind) && $self->isMySQLCompatible();
        
        push @{$self->config->validators}, 'QueryProperties' 
            if defined $self->grammar() && $self->grammar()->hasProperties() && $self->isMySQLCompatible();
    } else {
        ## Remove the "None" validator
        foreach my $i (0..$#{$self->config->validators}) {
            delete $self->config->validators->[$i] 
                if $self->config->validators->[$i] eq "None"
                or $self->config->validators->[$i] eq '';
        }
    }

    ## Add the transformer validator if --transformers is specified
    ## and transformer validator not allready specified.

    if (defined $self->config->transformers and 
        $#{$self->config->transformers} >= 0) 
    {
        my $hasTransformer = 0;
        foreach my $t (@{$self->config->validators}) {
            if ($t =~ /^Transformer/) {
                $hasTransformer = 1;
                last;
            }
        }
        push @{$self->config->validators}, 'Transformer' if !$hasTransformer;
    }

    say("Validators: ".(defined $self->config->validators and $#{$self->config->validators} > -1 ? join(', ', @{$self->config->validators}) : "(none)"));
    
    say("Transformers: ".join(', ', @{$self->config->transformers})) 
        if defined $self->config->transformers and $#{$self->config->transformers} > -1;

    return STATUS_OK;
}

sub copyLogFiles {
    my ($self, $logdir, $dsns) = @_;
    ## Do this only when tt-logging is enabled
    if (-e $self->config->property('report-tt-logdir')) {
        mkpath($logdir) if ! -e $logdir;

        # copy database logs
        foreach my $filename ($self->logFilesToReport()) {
            copyFileToDir($filename, $logdir);
        }
        # copy RQG log
        copyFileToDir($self->config->logfile, $logdir);
    }
}

sub copyFileToDir {
    my ($from, $todir) = @_;
    say("Copying '$from' to '$todir'");
    copy($from, $todir);
}


sub initXMLReport {
    my $self = shift;

    my $buildinfo;
    if (defined $self->config->property('xml-output')) {
        $buildinfo = GenTest::XML::BuildInfo->new(
            dsns => $self->config->dsn
        );
    }
    
    # XML: 
    #  Define test suite name for reporting purposes.
    #  Until we support test suites and/or reports with multiple suites/tests,
    #  we use the test name as test suite name, from config option "testname".
    #  Default test name is the basename portion of the grammar file name.
    #  If a grammar file is not given, the default is "rqg_no_name".
    my $test_suite_name = $self->config->testname;
    if (not defined $test_suite_name) {
        if (defined $self->config->grammar) {
            $test_suite_name = basename($self->config->grammar, '.yy');
        } else {
            $test_suite_name = "rqg_no_name";
        }
    }

    $self->[GT_XML_TEST] = GenTest::XML::Test->new(
        id => time(),
        name => $test_suite_name,  # NOTE: Consider changing to test (or test case) name when suites are supported.
        logdir => $self->config->property('report-tt-logdir').'/'.$test_suite_name.isoUTCSimpleTimestamp,
        attributes => {
            engine => $self->config->engine,
            gendata => $self->config->gendata,
            grammar => $self->config->grammar,
            threads => $self->config->threads,
            queries => $self->config->queries,
            validators => join (',', @{$self->config->validators}),
            reporters => join (',', @{$self->config->reporters}),
            seed => $self->config->seed,
            mask => $self->config->mask,
            mask_level => $self->config->property('mask-level'),
            rows => $self->config->rows,
            'varchar-length' => $self->config->property('varchar-length')
        }
    );
    
    $self->[GT_XML_REPORT] = GenTest::XML::Report->new(
        buildinfo => $buildinfo,
        name => $test_suite_name,  # NOTE: name here refers to the name of the test suite or "test".
        tests => [  $self->XMLTest() ]
    );
}

sub reportXMLIncidents {
    my ($self, $total_status, $incidents) = @_;
 
    foreach my $incident (@$incidents) {
        $self->XMLTest()->addIncident($incident);
    }
        
    # If no Reporters reported an incident, and we have a test failure,
    # create an incident report and add it to the test report.
    if ((scalar(@$incidents) < 1) && ($total_status != STATUS_OK)) {
        my $unreported_incident = GenTest::Incident->new(
            result      => 'fail',   # can we have other results as incidents?
            description => 'Non-zero status code from RQG test run',
            signature   => 'Exit status '.$total_status # better than nothing?
        );
        # Add the incident to the test report
        $self->XMLTest()->addIncident($unreported_incident);
    }
        
    $self->XMLTest()->end($total_status == STATUS_OK ? "pass" : "fail");
       
    if (defined $self->config->property('xml-output')) {
        open (XML , '>'.$self->config->property('xml-output')) or carp("Unable to open ".$self->config->property('xml-output').": $!");
        print XML $self->XMLReport()->xml();
        close XML;
        say("XML report written to ". $self->config->property('xml-output'));
    }

    # XML Result reporting to Test Tool (TT).
    # Currently both --xml-output=<filename> and --report-xml-tt must be
    # set to trigger this.
    if (defined $self->config->property('report-xml-tt')) {
        my $xml_transporter = GenTest::XML::Transporter->new(
            type => $self->config->property('report-xml-tt-type')
        );

        # If xml-output option is not set, bail out. TODO: Make xml-output optional.
        if (not defined $self->config->property('xml-output')) {
            carp("ERROR: --xml-output=<filename> must be set when using --report-xml-tt");
        }
 
        my $xml_send_result = $xml_transporter->sendXML(
            $self->config->property('xml-output'),
            $self->config->property('report-xml-tt-dest')
        );

        if ($xml_send_result != STATUS_OK) {
            croak("Error from XML Transporter: $xml_send_result");
        }

        if (defined $self->config->logfile && defined 
            $self->config->property('report-tt-logdir')) {
            $self->copyLogFiles($self->XMLTest->logdir(), $self->config->dsn);
        }
    }
}

1;

