# Copyright (c) 2008,2011 Oracle and/or its affiliates. All rights reserved.
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

package GenTest::Reporter;

require Exporter;
@ISA = qw(GenTest Exporter);
@EXPORT = qw(
	REPORTER_TYPE_PERIODIC
	REPORTER_TYPE_DEADLOCK
	REPORTER_TYPE_CRASH
	REPORTER_TYPE_SUCCESS
	REPORTER_TYPE_SERVER_KILLED
	REPORTER_TYPE_ALWAYS
	REPORTER_TYPE_DATA
);

use strict;
use GenTest;
use GenTest::Result;
use GenTest::Random;
use DBI;

use constant REPORTER_PRNG		=> 0;
use constant REPORTER_SERVER_DSN	=> 1;
use constant REPORTER_SERVER_VARIABLES	=> 2;
use constant REPORTER_SERVER_INFO	=> 3;
use constant REPORTER_SERVER_PLUGINS	=> 4;
use constant REPORTER_TEST_START	=> 5;
use constant REPORTER_TEST_END		=> 6;
use constant REPORTER_TEST_DURATION	=> 7;
use constant REPORTER_PROPERTIES	=> 8;

use constant REPORTER_TYPE_PERIODIC    	 => 2;
use constant REPORTER_TYPE_DEADLOCK    	 => 4;
use constant REPORTER_TYPE_CRASH       	 => 8;
use constant REPORTER_TYPE_SUCCESS	 => 16;
use constant REPORTER_TYPE_SERVER_KILLED => 32;
use constant REPORTER_TYPE_ALWAYS	 => 64;
use constant REPORTER_TYPE_DATA		 => 128;

1;

sub new {
	my $class = shift;

	my $reporter = $class->SUPER::new({
		dsn => REPORTER_SERVER_DSN,
		test_start => REPORTER_TEST_START,
		test_end => REPORTER_TEST_END,
		test_duration => REPORTER_TEST_DURATION,
		properties => REPORTER_PROPERTIES
	}, @_);

	my $dbh = DBI->connect($reporter->dsn(), undef, undef, { RaiseError => 0 , PrintError => 1 } );
	return undef if not defined $dbh;
	my $sth = $dbh->prepare("SHOW VARIABLES");

	$sth->execute();

	while (my $array_ref = $sth->fetchrow_arrayref()) {
		$reporter->[REPORTER_SERVER_VARIABLES]->{$array_ref->[0]} = $array_ref->[1];
	}

	$sth->finish();

	# SHOW SLAVE HOSTS may fail if user does not have the REPLICATION SLAVE privilege
	$dbh->{PrintError} = 0;
	my $slave_info = $dbh->selectrow_arrayref("SHOW SLAVE HOSTS");
	$dbh->{PrintError} = 1;
	if (defined $slave_info) {
		$reporter->[REPORTER_SERVER_INFO]->{slave_host} = $slave_info->[1];
		$reporter->[REPORTER_SERVER_INFO]->{slave_port} = $slave_info->[2];
	}

	if ($reporter->serverVariable('version') !~ m{^5\.0}sgio) {
		$reporter->[REPORTER_SERVER_PLUGINS] = $dbh->selectall_arrayref("
	                SELECT PLUGIN_NAME, PLUGIN_LIBRARY
	                FROM INFORMATION_SCHEMA.PLUGINS
	                WHERE PLUGIN_LIBRARY IS NOT NULL
	        ");
	}

	$dbh->disconnect();

	my $pid_file = $reporter->serverVariable('pid_file');

	open (PF, $pid_file);
	read (PF, my $pid, -s $pid_file);
	close (PF);

	$pid =~ s{[\r\n]}{}sio;

	$reporter->[REPORTER_SERVER_INFO]->{pid} = $pid;

	foreach my $server_path (
			'bin', 'sbin', 'sql', 'libexec',
			'../bin', '../sbin', '../sql', '../libexec',
			'../sql/RelWithDebInfo', '../sql/Debug',
		) {
		my $binary_unix = $reporter->serverVariable('basedir').'/'.$server_path."/mysqld";
		my $binary_windows = $reporter->serverVariable('basedir').'/'.$server_path."/mysqld.exe";

		if (
			(-e $binary_unix) ||
			(-e $binary_windows)
		) {
			$reporter->[REPORTER_SERVER_INFO]->{bindir} = $reporter->serverVariable('basedir').'/'.$server_path;
			$reporter->[REPORTER_SERVER_INFO]->{binary} = $binary_unix if -e $binary_unix;
			$reporter->[REPORTER_SERVER_INFO]->{binary} = $binary_windows if -e $binary_windows;
		}
	}

	foreach my $client_path (
		"client/RelWithDebInfo", "client/Debug",
		"client", "../client", "bin", "../bin"
	) {
	        if (-e $reporter->serverVariable('basedir').'/'.$client_path) {
			$reporter->[REPORTER_SERVER_INFO]->{'client_bindir'} = $reporter->serverVariable('basedir').'/'.$client_path;
	                last;
	        }
	}

	# look for error log relative to datadir
	foreach my $errorlog_path (
		"../log/master.err",  # MTRv1 regular layout
		"../log/mysqld1.err", # MTRv2 regular layout
		"../mysql.err"        # DBServer::MySQL layout
	) {
		my $possible_path = File::Spec->catfile($reporter->serverVariable('datadir'),$errorlog_path);
		if (-e $possible_path) {
			$reporter->[REPORTER_SERVER_INFO]->{'errorlog'} = $possible_path;
			last;
		}
	}

	my $prng = GenTest::Random->new( seed => 1 );
	$reporter->[REPORTER_PRNG] = $prng;

	return $reporter;
}

sub monitor {
	die "Default monitor() called.";
}

sub report {
	die "Default report() called.";
}

sub dsn {
	return $_[0]->[REPORTER_SERVER_DSN];
}

sub serverVariable {
	return $_[0]->[REPORTER_SERVER_VARIABLES]->{$_[1]};
}

sub serverInfo {
	$_[0]->[REPORTER_SERVER_INFO]->{$_[1]};
}

sub serverPlugins {
	return $_[0]->[REPORTER_SERVER_PLUGINS];
}

sub testStart {
	return $_[0]->[REPORTER_TEST_START];
}

sub testEnd {
	return $_[0]->[REPORTER_TEST_END];
}

sub prng {
	return $_[0]->[REPORTER_PRNG];
}

sub testDuration {
	return $_[0]->[REPORTER_TEST_DURATION];
}

sub properties {
	return $_[0]->[REPORTER_PROPERTIES];
}

sub configure {
    return 1;
}

1;
