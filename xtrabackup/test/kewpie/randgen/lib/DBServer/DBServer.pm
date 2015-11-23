# Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights
# reserved.
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

package DBServer::DBServer;
use base 'Exporter';

@EXPORT = ('say', 'sayFile', 'tmpdir', 'safe_exit', 
           'osWindows', 'osLinux', 'osSolaris', 'osMac',
           'isoTimestamp', 'isoUTCTimestamp',
           'DBSTATUS_OK','DBSTATUS_FAILURE');

use strict;

use Cwd;
use POSIX;
use Carp;

my $logger;
eval
{
    require Log::Log4perl;
    Log::Log4perl->import();
    $logger = Log::Log4perl->get_logger('randgen.dbserver');
};

use constant DBSTATUS_OK => 0;
use constant DBSTATUS_FAILURE => 1;

my $tmpdir;

1;

sub BEGIN {
	foreach my $tmp ($ENV{TMP}, $ENV{TEMP}, $ENV{TMPDIR}, '/tmp', '/var/tmp', cwd()."/tmp" ) {
		if (
			(defined $tmp) &&
			(-e $tmp)
		) {
			$tmpdir = $tmp;
			last;
		}
	}

	if (defined $tmpdir) {
		if (
			($^O eq 'MSWin32') ||
			($^O eq 'MSWin64')
		) {
			$tmpdir = $tmpdir.'\\';
		} else {
			$tmpdir = $tmpdir.'/';
		}
	}

	croak("Unable to locate suitable temporary directory.") if not defined $tmpdir;
	
	return 1;
}

sub new {
	my $class = shift;
	my $args = shift;

	my $obj = bless ([], $class);

        my $max_arg = (scalar(@_) / 2) - 1;

        foreach my $i (0..$max_arg) {
                if (exists $args->{$_[$i * 2]}) {
			if (defined $obj->[$args->{$_[$i * 2]}]) {
				carp("Argument '$_[$i * 2]' passed twice to ".$class.'->new()');
			} else {
	                        $obj->[$args->{$_[$i * 2]}] = $_[$i * 2 + 1];
			}
                } else {
                        carp("Unkown argument '$_[$i * 2]' to ".$class.'->new()');
                }
        }

        return $obj;
}

sub say {
	my $text = shift;
    defaultLogging();
    if ($text =~ m{[\r\n]}sio) {
        foreach my $line (split (m{[\r\n]}, $text)) {
            if (defined $logger) {
                $logger->info($line);
            } else {
                print "# ".isoTimestamp()." $line\n";
            }
        }
    } else {
        if (defined $logger) {
            $logger->info($text);
        } else {
            print "# ".isoTimestamp()." $text\n";
        }
    }
}

sub sayFile {
    my ($file) = @_;

    say("--------- Contents of $file -------------");
    open FILE,$file;
    while (<FILE>) {
	say("| ".$_);
    }
    close FILE;
    say("----------------------------------");
}

sub tmpdir {
	return $tmpdir;
}

sub safe_exit {
	my $exit_status = shift;
	POSIX::_exit($exit_status);
}

sub osWindows {
	if (
		($^O eq 'MSWin32') ||
	        ($^O eq 'MSWin64')
	) {
		return 1;
	} else {
		return 0;
	}	
}

sub osLinux {
	if ($^O eq 'linux') {
		return 1;
	} else {
		return 0;
	}
}

sub osSolaris {
	if ($^O eq 'solaris') {
		return 1;
	} else {
		return 0;
	}	
}

sub osMac {
    if ($^O eq 'darwin') {
        return 1;
    } else {
        return 0;
   }
}

sub isoTimestamp {
	my $datetime = shift;

	my ($sec, $min, $hour, $mday, $mon, $year, $wday, $yday, $isdst) = defined $datetime ? localtime($datetime) : localtime();
	return sprintf("%04d-%02d-%02dT%02d:%02d:%02d", $year+1900, $mon+1 ,$mday ,$hour, $min, $sec);

}

sub isoUTCTimestamp {
	my $datetime = shift;

	my ($sec, $min, $hour, $mday, $mon, $year, $wday, $yday, $isdst) = defined $datetime ? gmtime($datetime) : gmtime();
	return sprintf("%04d-%02d-%02dT%02d:%02d:%02d", $year+1900, $mon+1 ,$mday ,$hour, $min, $sec);
	
}

sub defaultLogging {
    if (defined $logger) {
        if (not Log::Log4perl::initialized()) {
            my $logconf = q(
log4perl.rootLogger = INFO, STDOUT
log4perl.appender.STDOUT=Log::Log4perl::Appender::Screen
log4perl.appender.STDOUT.layout=PatternLayout
log4perl.appender.STDOUT.layout.ConversionPattern=# %d{yyyy-MM-dd'T'HH:mm:ss} %m%n
);
            Log::Log4perl::init( \$logconf );
	    say("Using Log::Log4perl");
        }
    }
}

1;
