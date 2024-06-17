# Copyright (C) 2013 Monty Program Ab
# 
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.


package DBServer::MySQL::GaleraMySQLd;

@ISA = qw(DBServer::DBServer);

use DBI;
use DBServer::DBServer;
use DBServer::MySQL::MySQLd;

use strict;

use Carp;

use constant GALERA_MYSQLD_BASEDIR => 0;
use constant GALERA_MYSQLD_PARENT_VARDIR => 1;
use constant GALERA_MYSQLD_FIRST_PORT => 2;
use constant GALERA_MYSQLD_START_DIRTY => 3;
use constant GALERA_MYSQLD_SERVER_OPTIONS => 4;
use constant GALERA_MYSQLD_VALGRIND => 5;
use constant GALERA_MYSQLD_VALGRIND_OPTIONS => 6;
use constant GALERA_MYSQLD_GENERAL_LOG => 7;
use constant GALERA_MYSQLD_DEBUG_SERVER => 8;
use constant GALERA_MYSQLD_NODE_COUNT => 9;
use constant GALERA_MYSQLD_NODES => 10;

use constant GALERA_DEFAULT_LISTEN_PORT =>  4800;

##########################################
# The module starts a Galera cluster 
# with the requested number of nodes
##########################################

sub new {
	my $class = shift;
	my $self = $class->SUPER::new({
		'basedir' => GALERA_MYSQLD_BASEDIR,
		'debug_server' => GALERA_MYSQLD_DEBUG_SERVER,
		'parent_vardir' => GALERA_MYSQLD_PARENT_VARDIR,
		'first_port' => GALERA_MYSQLD_FIRST_PORT,
		'server_options' => GALERA_MYSQLD_SERVER_OPTIONS,
		'general_log' => GALERA_MYSQLD_GENERAL_LOG,
		'start_dirty' => GALERA_MYSQLD_START_DIRTY,
		'valgrind' => GALERA_MYSQLD_VALGRIND,
		'valgrind_options' => GALERA_MYSQLD_VALGRIND_OPTIONS,
		'node_count' => GALERA_MYSQLD_NODE_COUNT,
		'nodes' => GALERA_MYSQLD_NODES
	},@_);

	unless ($self->[GALERA_MYSQLD_NODE_COUNT]) {
		croak("No nodes defined");
	}

	$self->[GALERA_MYSQLD_NODES] = [];

# Set mandatory parameters as documented at 
# http://www.codership.com/wiki/doku.php?id=info#configuration_and_monitoring
# (except for wsrep_provider which has to be set by the user, as we don't know the path).
# Additionally we will set wsrep_sst_method=rsync as it makes the configuration simpler.
# It can be overridden from the command line if the user chooses so.
	my @common_options = (
		"--wsrep_on=ON",
		"--wsrep_sst_method=rsync", 
		"--innodb_autoinc_lock_mode=2", 
		"--default-storage-engine=InnoDB", 
		"--innodb_locks_unsafe_for_binlog=1",
		"--binlog-format=row"
	);

	if (not defined $self->[GALERA_MYSQLD_PARENT_VARDIR]) {
		$self->[GALERA_MYSQLD_PARENT_VARDIR] = "mysql-test/var";
	}

	foreach my $i (0..$self->[GALERA_MYSQLD_NODE_COUNT]-1) {
		
		my $port = $self->[GALERA_MYSQLD_FIRST_PORT]+$i;
		my $galera_listen_port = GALERA_DEFAULT_LISTEN_PORT + $i;
		my $galera_cluster_address = ($i ? "gcomm://127.0.0.1:".GALERA_DEFAULT_LISTEN_PORT : "gcomm://")
			. "?gmcast.listen_addr=tcp://127.0.0.1:".$galera_listen_port ;

		my $vardir = $self->[GALERA_MYSQLD_PARENT_VARDIR]."/node$i";

		my @node_options = ( 
			@common_options,
			"--wsrep_cluster_address=$galera_cluster_address"
		);

		if (defined $self->[GALERA_MYSQLD_SERVER_OPTIONS]) {
			push(@node_options, @{$self->[GALERA_MYSQLD_SERVER_OPTIONS]});
		}

		if ( $self->[GALERA_MYSQLD_NODE_COUNT] > 1 and ( join ',', @node_options ) !~ /wsrep[-_]provider/ ) {
			croak("ERROR: wsrep_provider is not set, replication between nodes is not possible");
		}

		$self->nodes->[$i] = DBServer::MySQL::MySQLd->new(
			basedir => $self->[GALERA_MYSQLD_BASEDIR],
			vardir => $vardir,
			debug_server => $self->[GALERA_MYSQLD_DEBUG_SERVER],                
			port => $port,
			server_options => \@node_options,
			general_log => $self->[GALERA_MYSQLD_GENERAL_LOG],
			start_dirty => $self->[GALERA_MYSQLD_START_DIRTY],
			valgrind => $self->[GALERA_MYSQLD_VALGRIND],
			valgrind_options => $self->[GALERA_MYSQLD_VALGRIND_OPTIONS]
		);
        
		if (not defined $self->nodes->[$i]) {
			croak("Could not create node $i");
		}
	}
	return $self;
}

sub nodes {
	return $_[0]->[GALERA_MYSQLD_NODES];
}

sub startServer {
	my ($self) = @_;
	# Path to Galera scripts is needed for SST 
	$ENV{PATH} = "$ENV{PATH}:$self->[GALERA_MYSQLD_BASEDIR]/scripts";
	foreach my $n (0..$self->[GALERA_MYSQLD_NODE_COUNT]-1) {
		return DBSTATUS_FAILURE if $self->nodes->[$n]->startServer != DBSTATUS_OK;

		my $node_dbh = $self->nodes->[$n]->dbh;
		my (undef, $cluster_size) = $node_dbh->selectrow_array("SHOW STATUS LIKE 'wsrep_cluster_size'");
		say("Cluster size after starting node $n: $cluster_size");
	}
	return DBSTATUS_OK;
}

sub waitForNodeSync {
# Stub
# TODO: check that all nodes have finished replication
    return DBSTATUS_OK;
}

sub stopServer {
	my ($self) = @_;

	foreach my $n (0..$self->[GALERA_MYSQLD_NODE_COUNT]-1) {
		$self->nodes->[$n]->stopServer;
	}
	return DBSTATUS_OK;
}

1;
