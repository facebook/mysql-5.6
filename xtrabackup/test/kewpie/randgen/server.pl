#!/usr/bin/perl

# Copyright (c) 2010 Oracle and/or its affiliates. All rights reserved.
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

use lib 'lib','lib/DBServer';
use lib "$ENV{RQG_HOME}/lib";
use Carp;
use strict;
use DBServer::DBServer;
use DBServer::MySQL::MySQLd;
use DBServer::MySQL::ReplMySQLd;

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
    $engine, $help, $debug, $sourcedir,
    $valgrind, @valgrind_options, 
    $start_dirty, $build_thread,$just_install);

my @ARGV_saved = @ARGV;

my $opt_result = GetOptions(
	'mysqld=s@' => \$mysqld_options[0],
	'mysqld1=s@' => \$mysqld_options[0],
	'mysqld2=s@' => \$mysqld_options[1],
    'basedir=s' => \$basedirs[0],
    'basedir1=s' => \$basedirs[0],
    'basedir2=s' => \$basedirs[1],
    'sourcedir=s' => \$sourcedir,
	#'basedir=s@' => \@basedirs,
	'vardir=s' => \$vardirs[0],
	'vardir1=s' => \$vardirs[0],
	'vardir2=s' => \$vardirs[1],
	#'vardir=s@' => \@vardirs,
	'rpl_mode=s' => \$rpl_mode,
	'engine=s' => \$engine,
	'help' => \$help,
	'debug' => \$debug,
	'valgrind!'	=> \$valgrind,
	'valgrind_options=s@'	=> \@valgrind_options,
	'start-dirty'	=> \$start_dirty,
    'mtr-build-thread=i' => \$build_thread,
    'just-install' => \$just_install
    );

if (!$opt_result || $help || $basedirs[0] eq '') {
	help();
	exit($help ? 0 : 1);
}

say("Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved. Use is subject to license terms.");
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

say("master_port : $ports[0] slave_port : $ports[1] ports : @ports MTR_BUILD_THREAD : $build_thread ") if !$just_install;

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
                                               start_dirty => $start_dirty);

    if (!$just_install) {
        my $status = $rplsrv->startServer();
        
        if ($status > STATUS_OK) {
            stopServers();
            say(system("ls -l ".$rplsrv->master->datadir));
            say(system("ls -l ".$rplsrv->slave->datadir));
            croak("Could not start replicating server pair");
        }
    
        $dsns[0] = $rplsrv->master->dsn($database);
        $dsns[1] = undef; ## passed to gentest. No dsn for slave!
        $server[0] = $rplsrv->master;
        $server[1] = $rplsrv->slave;
    }
        
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
                                                           sourcedir => $sourcedir,
                                                           vardir => $vardirs[$server_id],
                                                           port => $ports[$server_id],
                                                           start_dirty => $start_dirty,
                                                           valgrind => $valgrind,
                                                           valgrind_options => \@valgrind_options,
                                                           server_options => \@options);

        if (!$just_install) {
            my $status = $server[$server_id]->startServer;
            
            if ($status > STATUS_OK) {
                stopServers();
                say(system("ls -l ".$server[$server_id]->datadir));
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
}

if (!$just_install) {
    say("\n\nHit return to stop server(s)");
    
    my $something = <STDIN>;
    
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
    
}

sub help {
    
	print <<EOF
Copyright (c) 2010 Oracle and/or its affiliates. All rights reserved. Use is subject to license terms.

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

    --just-install: Just create the database(s) (mysql_install_db), don't start the server
    --grammar   : Grammar file to use when generating queries (REQUIRED);
    --redefine  : Grammar file to redefine and/or add rules to the given grammar
    --rpl_mode  : Replication type to use (statement|row|mixed) (default: no replication);
    --vardir1   : Optional.
    --vardir2   : Optional. 
    --engine    : Table engine to use when creating tables with gendata (default no ENGINE in CREATE TABLE);
    --valgrind  : Passed to gentest.pl
    --mtr-build-thread: 
                  Value used for MTR_BUILD_THREAD when servers are started and accessed.
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
    stopServers();
	say("[$$] $0 will exit with exit status $status");
	safe_exit($status);
}
