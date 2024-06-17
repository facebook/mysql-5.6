# Copyright (c) 2008,2012 Oracle and/or its affiliates. All rights reserved.
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
	REPORTER_TYPE_END
);

use strict;
use GenTest;
use GenTest::Result;
use GenTest::Random;
use DBI;
use File::Find;
use File::Spec;
use Carp;

use constant REPORTER_PRNG              => 0;
use constant REPORTER_SERVER_DSN        => 1;
use constant REPORTER_SERVER_VARIABLES  => 2;
use constant REPORTER_SERVER_INFO       => 3;
use constant REPORTER_SERVER_PLUGINS    => 4;
use constant REPORTER_TEST_START        => 5;
use constant REPORTER_TEST_END          => 6;
use constant REPORTER_TEST_DURATION     => 7;
use constant REPORTER_PROPERTIES        => 8;
use constant REPORTER_SERVER_DEBUG      => 9;
use constant REPORTER_CUSTOM_ATTRIBUTES => 10;
# TEST_START is when the RQG test started running; 
# REPORTER_START_TIME is when the data has been generated, and reporter was started
# (more or less when the test flow started)
use constant REPORTER_START_TIME        => 11; 

use constant REPORTER_TYPE_PERIODIC     => 2;
use constant REPORTER_TYPE_DEADLOCK     => 4;
use constant REPORTER_TYPE_CRASH        => 8;
use constant REPORTER_TYPE_SUCCESS      => 16;
use constant REPORTER_TYPE_SERVER_KILLED    => 32;
use constant REPORTER_TYPE_ALWAYS       => 64;
use constant REPORTER_TYPE_DATA         => 128;
# New reporter type which can be used at the end of a test.
use constant REPORTER_TYPE_END          => 256;

1;

sub new {
	my $class = shift;

	my $reporter = $class->SUPER::new({
		dsn => REPORTER_SERVER_DSN,
		test_start => REPORTER_TEST_START,
		test_end => REPORTER_TEST_END,
		test_duration => REPORTER_TEST_DURATION,
		debug_server => REPORTER_SERVER_DEBUG,
		properties => REPORTER_PROPERTIES
	}, @_);

	my $dbh = DBI->connect($reporter->dsn(), undef, undef, { mysql_multi_statements => 1, RaiseError => 0 , PrintError => 1 } );
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

	$reporter->updatePid();

    my $binary;
    my $bindir;
    my $binname;
    # Use debug server, mysqld_debug.
    if ($reporter->serverDebug){
        $binname = osWindows() ? 'mysqld-debug.exe' : 'mysqld-debug';
        ($bindir,$binary)=$reporter->findMySQLD($binname);
        if ((-e $binary)) {
            $reporter->[REPORTER_SERVER_INFO]->{bindir} = $bindir;
            $reporter->[REPORTER_SERVER_INFO]->{binary} = $binary;
        } else {
            # If mysqld_debug server is not present use mysqld.
            $binname = osWindows() ? 'mysqld.exe' : 'mysqld';
            ($bindir,$binary)=$reporter->findMySQLD($binname);
            
            # Identify if server is debug.
            my $command = $binary.' --version';
            my $result=`$command 2>&1`;
            undef $binary if ($result !~ /debug/sig);
            
            if ((-e $binary)) {
                $reporter->[REPORTER_SERVER_INFO]->{bindir} = $bindir;
                $reporter->[REPORTER_SERVER_INFO]->{binary} = $binary;
            }
        }
    } else {
        # Use non-debug serever.
        $binname = osWindows() ? 'mysqld.exe' : 'mysqld';
        ($bindir,$binary)=$reporter->findMySQLD($binname);
        if ((-e $binary)) {
            $reporter->[REPORTER_SERVER_INFO]->{bindir} = $bindir;
            $reporter->[REPORTER_SERVER_INFO]->{binary} = $binary;
        }else {
            # If we dont find non-debug server use debug(mysqld_debug) server.
            $binname = osWindows() ? 'mysqld-debug.exe' : 'mysqld-debug';
            ($bindir,$binary)=$reporter->findMySQLD($binname);
            if ((-e $binary)) {
                $reporter->[REPORTER_SERVER_INFO]->{bindir} = $bindir;
                $reporter->[REPORTER_SERVER_INFO]->{binary} = $binary;
            }
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

	# general properties area for sub-classes
	$reporter->[REPORTER_CUSTOM_ATTRIBUTES]={};
	$reporter->[REPORTER_START_TIME]= time();

	return $reporter;
}

sub updatePid {
	my $pid_file = $_[0]->serverVariable('pid_file');

	open (PF, $pid_file);
	read (PF, my $pid, -s $pid_file);
	close (PF);

	$pid =~ s{[\r\n]}{}sio;

	$_[0]->[REPORTER_SERVER_INFO]->{pid} = $pid;
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

sub reporterStartTime {
	return $_[0]->[REPORTER_START_TIME];
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

sub serverDebug {
    return $_[0]->[REPORTER_SERVER_DEBUG];
}

sub customAttribute() {
	if (defined $_[2]) {
		$_[0]->[GenTest::Reporter::REPORTER_CUSTOM_ATTRIBUTES]->{$_[1]}=$_[2];
	}
	return $_[0]->[GenTest::Reporter::REPORTER_CUSTOM_ATTRIBUTES]->{$_[1]};
}

sub configure {
    return 1;
}

# Input : For given binary either mysqld/mysqld-debug
# Output: Return bindir and absolute path of the binary file.
sub findMySQLD {
    my ($reporter,$binname)=@_;
    my $bindir;
    # Handling general basedirs and MTRv1 style basedir,
    # but trying not to search the entire universe just for the sake of it
    my @basedirs = ($reporter->serverVariable('basedir'));
    if (! -e File::Spec->catfile($reporter->serverVariable('basedir'),'mysql-test') and -e File::Spec->catfile($reporter->serverVariable('basedir'),'t')) {
        # Assuming it's the MTRv1 style basedir
        @basedirs=(File::Spec->catfile($reporter->serverVariable('basedir'),'..'));
    }
    find(sub {
            $bindir=$File::Find::dir if $_ eq $binname;
    }, @basedirs);
    my $binary = File::Spec->catfile($bindir, $binname);
    return ($bindir,$binary);
}

1;
