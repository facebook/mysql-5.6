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

package DBServer::MySQL::ReplMySQLd;

@ISA = qw(DBServer::DBServer);

use DBI;
use DBServer::DBServer;
use DBServer::MySQL::MySQLd;
use if osWindows(), Win32::Process;
use Time::HiRes;

use strict;

use Carp;
use Data::Dumper;

use constant REPLMYSQLD_BASEDIR => 0;
use constant REPLMYSQLD_MASTER_VARDIR => 1;
use constant REPLMYSQLD_SLAVE_VARDIR => 2;
use constant REPLMYSQLD_MASTER_PORT => 3;
use constant REPLMYSQLD_SLAVE_PORT => 4;
use constant REPLMYSQLD_MODE => 5;
use constant REPLMYSQLD_START_DIRTY => 6;
use constant REPLMYSQLD_SERVER_OPTIONS => 7;
use constant REPLMYSQLD_MASTER => 8;
use constant REPLMYSQLD_SLAVE => 9;
use constant REPLMYSQLD_VALGRIND => 10;
use constant REPLMYSQLD_VALGRIND_OPTIONS => 11;

sub new {
    my $class = shift;

    my $self = $class->SUPER::new({'master' => REPLMYSQLD_MASTER,
                                   'slave' => REPLMYSQLD_SLAVE,
                                   'basedir' => REPLMYSQLD_BASEDIR,
                                   'master_vardir' => REPLMYSQLD_MASTER_VARDIR,
                                   'master_port' => REPLMYSQLD_MASTER_PORT,
                                   'slave_vardir' => REPLMYSQLD_SLAVE_VARDIR,
                                   'slave_port' => REPLMYSQLD_SLAVE_PORT,
                                   'mode' => REPLMYSQLD_MODE,
                                   'server_options' => REPLMYSQLD_SERVER_OPTIONS,
                                   'start_dirty' => REPLMYSQLD_START_DIRTY,
                                   'valgrind' => REPLMYSQLD_VALGRIND,
                                   'valgrind_options', REPLMYSQLD_VALGRIND_OPTIONS},@_);
    
    if (defined $self->master || defined $self->slave) {
        ## Repl pair defined from two predefined servers

        if (not (defined $self->master && defined $self->slave)) {
            croak("Both master and slave must be defined");
        }
        $self->master->addServerOptions(["--server_id=1",
                                         "--log-bin=mysql-bin",
                                         "--report-host=127.0.0.1",
                                         "--report_port=".$self->master->port]);
        $self->slave->addServerOptions(["--server_id=2",
                                        "--report-host=127.0.0.1",
                                        "--report_port=".$self->slave->port]);
    } else {
        ## Repl pair defined from parameters. The servers have the same basedir (is of the same version)
        if (not defined $self->[REPLMYSQLD_MASTER_PORT]) {
            $self->[REPLMYSQLD_MASTER_PORT] = DBServer::MySQL::MySQLd::MYSQLD_DEFAULT_PORT;
        }
    
        if (not defined $self->[REPLMYSQLD_SLAVE_PORT]) {
            $self->[REPLMYSQLD_SLAVE_PORT] = $self->[REPLMYSQLD_MASTER_PORT] + 2;        
        }

        if (not defined $self->[REPLMYSQLD_MODE]) {
            $self->[REPLMYSQLD_MODE] = 'default';
        }
    
        if (not defined $self->[REPLMYSQLD_MASTER_VARDIR]) {
            $self->[REPLMYSQLD_MASTER_VARDIR] = "mysql-test/var";
        }
        if (not defined $self->[REPLMYSQLD_SLAVE_VARDIR]) {
            my $varbase = $self->[REPLMYSQLD_MASTER_VARDIR];
            $varbase =~ s/(.*)\/$/\1/;
            $self->[REPLMYSQLD_SLAVE_VARDIR] = $varbase.'_slave';
        }
        
        my @master_options;
        push(@master_options, 
             "--server_id=1",
             "--log-bin=mysql-bin",
             "--report-host=127.0.0.1",
             "--report_port=".$self->[REPLMYSQLD_MASTER_PORT]);
        if (defined $self->[REPLMYSQLD_SERVER_OPTIONS]) {
            push(@master_options, 
                 @{$self->[REPLMYSQLD_SERVER_OPTIONS]});
        }
        
        
        $self->[REPLMYSQLD_MASTER] = 
            DBServer::MySQL::MySQLd->new(basedir => $self->[REPLMYSQLD_BASEDIR],
                                         vardir => $self->[REPLMYSQLD_MASTER_VARDIR],
                                         port => $self->[REPLMYSQLD_MASTER_PORT],
                                         server_options => \@master_options,
                                         start_dirty => $self->[REPLMYSQLD_START_DIRTY],
                                         valgrind => $self->[REPLMYSQLD_VALGRIND],
                                         valgrind_options => $self->[REPLMYSQLD_VALGRIND_OPTIONS]);
        
        if (not defined $self->master) {
            croak("Could not create master");
        }
        
        my @slave_options;
        push(@slave_options, 
             "--server_id=2",
             "--report-host=127.0.0.1",
             "--report_port=".$self->[REPLMYSQLD_SLAVE_PORT]);
        if (defined $self->[REPLMYSQLD_SERVER_OPTIONS]) {
            push(@slave_options, 
                 @{$self->[REPLMYSQLD_SERVER_OPTIONS]});
        }
        
        
        $self->[REPLMYSQLD_SLAVE] = 
            DBServer::MySQL::MySQLd->new(basedir => $self->[REPLMYSQLD_BASEDIR],
                                         vardir => $self->[REPLMYSQLD_SLAVE_VARDIR],
                                         port => $self->[REPLMYSQLD_SLAVE_PORT],
                                         server_options => \@slave_options,
                                         start_dirty => $self->[REPLMYSQLD_START_DIRTY],
                                         valgrind => $self->[REPLMYSQLD_VALGRIND],
                                         valgrind_options => $self->[REPLMYSQLD_VALGRIND_OPTIONS]);
        
        if (not defined $self->slave) {
            $self->master->stopServer;
            croak("Could not create slave");
        }
    }
    
    return $self;
}

sub master {
    return $_[0]->[REPLMYSQLD_MASTER];
}

sub slave {
    return $_[0]->[REPLMYSQLD_SLAVE];
}

sub mode {
    return $_[0]->[REPLMYSQLD_MODE];
}

sub startServer {
    my ($self) = @_;

    $self->master->startServer;
    my $master_dbh = $self->master->dbh;
    $self->slave->startServer;
    my $slave_dbh = $self->slave->dbh;

	my ($foo, $master_version) = $master_dbh->selectrow_array("SHOW VARIABLES LIKE 'version'");

	if (($master_version !~ m{^5\.0}sio) && ($self->mode ne 'default')) {
		$master_dbh->do("SET GLOBAL BINLOG_FORMAT = '".$self->mode."'");
		$slave_dbh->do("SET GLOBAL BINLOG_FORMAT = '".$self->mode."'");
	}
    
	$slave_dbh->do("STOP SLAVE");

#	$slave_dbh->do("SET GLOBAL storage_engine = '$engine'") if defined $engine;
    
	$slave_dbh->do("CHANGE MASTER TO ".
                   " MASTER_PORT = ".$self->master->port.",".
                   " MASTER_HOST = '127.0.0.1',".
                   " MASTER_USER = 'root',".
                   " MASTER_CONNECT_RETRY = 1");
    
	$slave_dbh->do("START SLAVE");
    
    return DBSTATUS_OK;
}

sub waitForSlaveSync {
    my ($self) = @_;
    my ($file, $pos) = $self->master->dbh->selectrow_array("SHOW MASTER STATUS");
    say("master status $file/$pos");
    my $wait_result = $self->slave->dbh->selectrow_array("SELECT MASTER_POS_WAIT('$file',$pos)");
    if (not defined $wait_result) {
        my @slave_status = $self->slave->dbh->selectrow_array("SHOW SLAVE STATUS");
        say("Slave SQL thread has stopped with error: ".$slave_status[37]);
		return DBSTATUS_FAILURE;
    } else {
        return DBSTATUS_OK;
    }
}

sub stopServer {
    my ($self) = @_;

    $self->waitForSlaveSync();
    $self->slave->dbh->do("STOP SLAVE");
    
    $self->slave->stopServer;
    $self->master->stopServer;

    return DBSTATUS_OK;
}

1;
