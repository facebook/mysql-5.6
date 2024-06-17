# Copyright (c) 2012 Oracle and/or its affiliates. All rights reserved.
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

package TestReporter;

use strict;
use warnings;
use base qw(Test::Unit::TestCase);
use lib 'lib','lib/DBServer';
use Cwd;
use GenTest;
use DBServer::DBServer;
use DBServer::MySQL::MySQLd;
use GenTest::Executor;
use GenTest::Properties;
use GenTest::Constants;
use GenTest::Reporter;
use GenTest::ReporterManager;
use GenTest::Reporter::Backtrace;
use File::Basename;

use File::Path qw(mkpath rmtree);

sub new {
    my $self = shift()->SUPER::new(@_);
    # your state for fixture here
    return $self;
}

my @pids;
my $server;
my $executor;
my $vardir;

sub set_up {
    if (not osWindows()) { ## crash is not yet implemented for windows
        my $self = shift;
        
        my $portbase = 40 + ($ENV{TEST_PORTBASE}?int($ENV{TEST_PORTBASE}):22120);
        
        $vardir= cwd()."/unit/tmpwd";
        
        $self->assert(defined $ENV{RQG_MYSQL_BASE},"RQG_MYSQL_BASE not defined");
        
        $server = DBServer::MySQL::MySQLd->new(basedir => $ENV{RQG_MYSQL_BASE},
                                               vardir => $vardir,
                                               port => $portbase);
        $self->assert_not_null($server);
        
        $self->assert(-f $vardir."/data/mysql/db.MYD","No ".$vardir."/data/mysql/db.MYD");
        
        $server->startServer;
        push @pids,$server->serverpid;
        
        my $dsn = $server->dsn("mysql");
        $self->assert_not_null($dsn);
        
        $executor = GenTest::Executor->newFromDSN($dsn);
        $self->assert_not_null($executor);
        $executor->init();
        
        my $result = $executor->execute("show tables");
        $self->assert_not_null($result);
        $self->assert_equals($result->status, 0);
        
        say(join(',',map{$_->[0]} @{$result->data}));
        
        $self->assert(-f $vardir."/mysql.pid") if not osWindows();
        $self->assert(-f $vardir."/mysql.err");
    }
}

sub tear_down {
    if (osWindows) {
        ## Need to ,kill leftover processes if there are some
        foreach my $p (@pids){
            Win32::Process::KillProcess($p,-1);
        }
    } else {
        ## Need to ,kill leftover processes if there are some
        kill 9 => @pids;
    }
    rmtree("unit/tmpwd");
}

# Routine to initilaize reporter.
sub init_reporter {
    my ($self,$reporter)=@_;
    
    # Intialize the reporters.
    my $reporter_manager = GenTest::ReporterManager->new();
    $self->assert_not_null($reporter_manager);
    
    $reporter_manager->addReporter($reporter, { dsn => $server->dsn,
                                                properties =>  GenTest::Properties->new()
    });

    $self->assert_not_null($reporter_manager);
    
    return $reporter_manager;
}

# To test a server crash and how bactrace reporter works.
sub test_crash_and_backtrace_reporter {
    my $self = shift;
    
    # Initialize the reporter.
    my $reporter="Backtrace";
    my $reporter_manager=$self->init_reporter($reporter);
    
    # Crash the server for the test.
    if (not osWindows()) { ## crash is not yet implemented for windows
        sleep(1);
        $server->crash;
        sleep(1);
    }
    
    # Display the server log.
    sayFile($server->errorlog);
    say("Core: ". $server->corefile);
    
    # Start the reporter.
    $reporter_manager->report(REPORTER_TYPE_CRASH);
}


# Bug 13625712
# Fix Valgrind reporter that should not be loaded when a server crashes.
sub test_crash_and_valgrind_reporter {
    my $self = shift;
    
    # Initialize the reporter.
    my $reporter="ValgrindErrors";
    my $reporter_manager = $self->init_reporter($reporter);
    
    # Crash the server for the test.
    if (not osWindows()) { ## crash is not yet implemented for windows
        sleep(1);
        $server->crash;
        sleep(1);
    }
    
    # Display the server log.
    sayFile($server->errorlog);
    say("Core: ". $server->corefile);
    
    # Hack to introduce a valgrind error.
    # TODO, if fails find alternative way to introduce a failure.
    open (FH,">>",$vardir."/mysql.err") or die $!;
    print FH 'conditional jump';
    close(FH);
    
    # This execution should fail.
    my $result = $executor->execute("show tables");
    $self->assert_not_null($result);
    
    my $status=$result->status();
    $self->assert_not_null($status);
    
    # Start the reporter.
    # Verify that the reporter is not picked, before this bugfix it will fail.
    my @report_results;
    if ($status == STATUS_SERVER_CRASHED) {
        @report_results=$reporter_manager->report(REPORTER_TYPE_CRASH|REPORTER_TYPE_ALWAYS);
        $self->assert_not_null(@report_results);
    }
    my $report_status = shift @report_results;
    $self->assert_not_null($report_status);
    
    # Verify that the status is not changed after a server crash.
    my $total_status=$status;
    $total_status = $report_status if $report_status > $status;
    $self->assert_not_null($total_status);
    
    # Check the original execution status and new status are the same.
    $self->assert_equals($status,$total_status);
}


# Check the type of binary mysqld or mysqld-debug
sub check_binary_type {
    my ($self,$reporter)=@_;
    my $binary;
    my $file_name;
    
    $binary=$reporter->serverInfo('binary');
    # If the server binary is debug
    if ($reporter->serverDebug) {
        $binary=$reporter->serverInfo('binary');
        $self->assert_not_null($binary);
        
        $file_name=basename($binary);
        $self->assert_not_null($file_name);
        
        # type should be mysqld-debug
        if ($file_name eq 'mysqld-debug') {
            $self->assert_equals($file_name,'mysqld-debug');
        } else {
        	# debug binaries can be mysqld too
            $self->assert_equals($file_name,'mysqld');  
        }
    } else {
        # if the server is non-debug
        $binary=$reporter->serverInfo('binary');
        $self->assert_not_null($binary);
        
        $file_name=basename($binary);
        $self->assert_not_null($file_name);
        
        # sever binary type should be mysqld
        if ( $file_name eq 'mysqld') {
            $self->assert_equals($file_name,'mysqld');
        } else {
        	# in case we have a mysqld-debug binary
            $self->assert_equals($file_name,'mysqld-debug');    
        }
    }
}

# Bug:14328581 
# To test mysqld/mysqld-debug binary type servers.
sub test_mysqld_debug_and_nondebug_server_with_reporter {
    my $self = shift;
    
    # Intialize the reporters.
    
    if (defined $ENV{RQG_TEST_DEBUG_SERVER}) {
        my $reporter1 = GenTest::Reporter->new(dsn => $server->dsn,
                                               debug_server => 1,
                                               properties =>  GenTest::Properties->new());
        $self->assert_not_null($reporter1);
        $self->check_binary_type($reporter1);
    }
    
    my $reporter2 = GenTest::Reporter->new(dsn => $server->dsn,
                                           properties =>  GenTest::Properties->new());
    $self->assert_not_null($reporter2);
    $self->check_binary_type($reporter2);
}

1;
