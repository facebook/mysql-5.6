# Copyright (C) 2010 Sun Microsystems, Inc. All rights reserved.  Use
# is subject to license terms.
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

package TestReplServer;

use base qw(Test::Unit::TestCase);
use lib 'lib','lib/DBServer';
use Cwd;
use DBServer::DBServer;
use DBServer::MySQL::ReplMySQLd;
use GenTest::Executor;

use Data::Dumper;
use File::Path qw(mkpath rmtree);

sub new {
    my $self = shift()->SUPER::new(@_);
    # your state for fixture here
    return $self;
}

sub set_up {
}

@pids;

sub tear_down {
    if (osWindows) {
        ## Need to ,kill leftover processes if there are some
        foreach my $p (@pids) {
            Win32::Process::KillProcess($p,-1);
        }
    } else {
        ## Need to ,kill leftover processes if there are some
        kill 9 => @pids;
    }
    rmtree("unit/tmp1");
    rmtree("unit/tmp1_slave");
}


sub test_create_server {
    my $self = shift;
    
    my $portbase = 30 + ($ENV{TEST_PORTBASE}?int($ENV{TEST_PORTBASE}):22120);

    my $master_vardir= cwd()."/unit/tmp1/";
    my $slave_vardir= cwd()."/unit/tmp1_slave";
    
    $self->assert(defined $ENV{RQG_MYSQL_BASE},"RQG_MYSQL_BASE not defined");
    
    my $server = DBServer::MySQL::ReplMySQLd->new(basedir => $ENV{RQG_MYSQL_BASE},
                                                  master_vardir => $master_vardir,
                                                  mode => 'statement',
                                                  master_port => $portbase);
    $self->assert_not_null($server);
    
    $self->assert(-f $master_vardir."/data/mysql/db.MYD","No ".$master_vardir."/data/mysql/db.MYD");
    $self->assert(-f $slave_vardir."/data/mysql/db.MYD","No ".$slave_vardir."/data/mysql/db.MYD");
    
    $server->startServer;
    push @pids,$server->master->serverpid;
    push @pids,$server->slave->serverpid;


    $server->master->dbh->do("CREATE TABLE test.t (i integer)");
    $server->master->dbh->do("INSERT INTO test.t VALUES(42)");

    $server->waitForSlaveSync();
    
    my $result = $server->slave->dbh->selectrow_array("SELECT * FROM test.t");
    
    $self->assert_num_equals(42, $result);
    
    $server->stopServer;
    
    sayFile($server->master->errorlog);
    sayFile($server->slave->errorlog);
}

sub test_create_repl {
    my $self = shift;
    
    my $portbase = 30 + ($ENV{TEST_PORTBASE}?int($ENV{TEST_PORTBASE}):22120);

    my $master_vardir= cwd()."/unit/tmp1/";
    my $slave_vardir= cwd()."/unit/tmp1_slave";
    
    $self->assert(defined $ENV{RQG_MYSQL_BASE},"RQG_MYSQL_BASE not defined");
    
    my $master = DBServer::MySQL::MySQLd->new(basedir => $ENV{RQG_MYSQL_BASE},
                                              vardir => $master_vardir,
                                              port => $portbase);
    my $slave = DBServer::MySQL::MySQLd->new(basedir => $ENV{RQG_MYSQL_BASE},
                                             vardir => $slave_vardir,
                                             port => $portbase+2);
    
    my $server = DBServer::MySQL::ReplMySQLd->new(slave => $slave,
                                                  master => $master,
                                                  mode => 'mixed');
                                             
    $self->assert_not_null($server);
    
    $self->assert(-f $master_vardir."/data/mysql/db.MYD","No ".$master_vardir."/data/mysql/db.MYD");
    $self->assert(-f $slave_vardir."/data/mysql/db.MYD","No ".$slave_vardir."/data/mysql/db.MYD");
    
    $server->startServer;
    push @pids,$server->master->serverpid;
    push @pids,$server->slave->serverpid;


    $server->master->dbh->do("CREATE TABLE test.t (i integer)");
    $server->master->dbh->do("INSERT INTO test.t VALUES(42)");

    $server->waitForSlaveSync();
    
    my $result = $server->slave->dbh->selectrow_array("SELECT * FROM test.t");
    
    $self->assert_num_equals(42, $result);
    
    $server->stopServer;
    
    sayFile($server->master->errorlog);
    sayFile($server->slave->errorlog);
}

1;
