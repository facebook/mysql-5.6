# Copyright (c) 2011 Oracle and/or its affiliates. All rights reserved.
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

################################################################################
# Test the --sqltrace option.
#
# Expected behavior:
#  --sqltrace : All generated/executed SQL statements are printed to std out/logfile.
#  --sqltrace=MarkErrors : Same as --sqltrace, except that invalid statements will
#                          be marked by a prefix like "# [sqltrace] ERROR <errNo>:"
#  --sqltrace=<invalid_value> : RQG should not start any test runs and give an error message.

package TestSqltrace;
use base qw(Test::Unit::TestCase);
use lib 'lib';
use GenTest;
use Cwd;
use GenTest::Constants;

my $counter;    # something to distinguish test cases to avoid port conflicts etc.

sub new {
    my $self = shift()->SUPER::new(@_);
    $ENV{LD_LIBRARY_PATH}=join(":",map{"$ENV{RQG_MYSQL_BASE}".$_}("/libmysql/.libs","/libmysql","/lib/mysql"));
    
    # These tests needs to use a grammar file that is known to produce:
    #  1) Some valid SQL statements
    #  2) At least one invalid SQL statement
    # We expect 4 queries in total from the test grammar when --queries=2.
    my $grammar = 'unit/testSqltrace.yy';
    # We construct the main part of the RQG command line here. Some parts need
    # to be set later:
    #   --mtr-build-thread : Should differ between testcases due to parallelism.
    #   runall.pl or runall-new.pl : We need to test both.
    #   Output redirection.
    my $rqg_opts = "--grammar=$grammar " 
            .'--queries=2 ' 
            .'--threads=1 ' 
            .'--basedir='.$ENV{RQG_MYSQL_BASE};
    $self->{rqg_command_runall} = 'perl -MCarp=verbose ./runall.pl '.$rqg_opts.' --reporter=Shutdown'; 
    $self->{rqg_command_runall_new} = 'perl -MCarp=verbose ./runall-new.pl '.$rqg_opts;
    # Pattern to look for in rqg output / logfile.
    $self->{trace_prefix_pattern} = '^# \[sqltrace\] ERROR [0-9]+:';
    return $self;
}

sub set_up {
    my $self = shift;
    $counter++;
    $self->{logfile} = 'unit/sqltrace'.$counter.'.log';
    my $portbase = ($counter*10) + ($ENV{TEST_PORTBASE}>0 ? int($ENV{TEST_PORTBASE}) : 22120);
    $self->{pb} = int(($portbase - 10000) / 10);
}

sub tear_down {
    my $self = shift;
    # clean up after test
    unlink $self->{logfile};
}

sub test_sqltrace_disabled_runall {
    my $self = shift;
    ## This test requires RQG_MYSQL_BASE to point to a MySQL installation (or in-source build)
    if ($ENV{RQG_MYSQL_BASE}) {
        # We do *not* specify --sqltrace, so no statements should be traced.
        my $cmd = $self->{rqg_command_runall}.' --mtr-build-thread='.$self->{pb}
        .' > '.$self->{logfile}.' 2>&1';
        $self->annotate("RQG command line: $cmd");
        my $status = system($cmd);
        my $expected = STATUS_OK;
        my $actual = $status >> 8;
        $self->assert_num_equals($expected, $actual, 
            "Wrong exit status from runall.pl, expected $expected and got $actual");
        
        # Read log and count lines starting with "SELECT pk".
        # We expect 0 lines on basis of test grammar and RQG settings.
        my $expected = 0;
        my $lines = 0;
        open(LOGFILE, "<", $self->{logfile}) or $self->assert("Unable to read log file ".$self->{logfile});
        while (my $line = <LOGFILE>) {
            # we look for both prefixed and non-prefixed lines with "SELECT pk"
            $lines++ if $line =~ m{^(# \[sqltrace\].*|)\s*SELECT pk};
        }
        close(LOGFILE);
        $self->assert_num_equals($expected, $lines, 
                "Unexpected number of traced statements: Expected $expected, got $lines");
    }
}

sub test_sqltrace_disabled_runall_new {
    my $self = shift;
    ## This test requires RQG_MYSQL_BASE to point to a MySQL installation (or in-source build)
    if ($ENV{RQG_MYSQL_BASE}) {
        # We do *not* specify --sqltrace, so no statements should be traced.
        my $cmd = $self->{rqg_command_runall_new}.' --mtr-build-thread='.$self->{pb}
        .' > '.$self->{logfile}.' 2>&1';
        $self->annotate("RQG command line: $cmd");
        my $status = system($cmd);
        my $expected = STATUS_OK;
        my $actual = $status >> 8;
        $self->assert_num_equals($expected, $actual, 
            "Wrong exit status from runall-new.pl, expected $expected and got $actual");
        
        # Read log and count lines starting with "SELECT pk".
        # We expect 0 lines on basis of test grammar and RQG settings.
        my $expected = 0;
        my $lines = 0;
        open(LOGFILE, "<", $self->{logfile}) or $self->assert("Unable to read log file ".$self->{logfile});
        while (my $line = <LOGFILE>) {
            # we look for both prefixed and non-prefixed lines with "SELECT pk"
            $lines++ if $line =~ m{^(# \[sqltrace\].*|)\s*SELECT pk};
        }
        close(LOGFILE);
        $self->assert_num_equals($expected, $lines, 
                "Unexpected number of traced statements: Expected $expected, got $lines");
    }
}

sub test_sqltrace_default_runall {
    my $self = shift;
    ## This test requires RQG_MYSQL_BASE to point to a MySQL installation (or in-source build)
    if ($ENV{RQG_MYSQL_BASE}) {
        # We specify --sqltrace without a value. This should result in all statements
        # being traced, without marking invalid queries.
        my $cmd = $self->{rqg_command_runall}.' --mtr-build-thread='.$self->{pb}
        .' --sqltrace > '.$self->{logfile}.' 2>&1';
        $self->annotate("RQG command line: $cmd");
        my $status = system($cmd);
        my $expected = STATUS_OK;
        my $actual = $status >> 8;
        $self->assert_num_equals($expected, $actual, 
            "Wrong exit status from runall.pl, expected $expected and got $actual");
        
        # Read log and count lines starting with "SELECT pk".
        # We expect 4 lines on basis of test grammar producing dual statements
        # and --queries=2
        my $expected = 4;
        my $lines = 0;
        open(LOGFILE, "<", $self->{logfile}) or $self->assert("Unable to read log file ".$self->{logfile});
        while (my $line = <LOGFILE>) {
            $lines++ if $line =~ m{^\s*SELECT pk};
        }
        close(LOGFILE);
        $self->assert_num_equals($expected, $lines, 
                "Unexpected number of traced statements: Expected $expected, got $lines");
    }
}

sub test_sqltrace_default_runall_new {
    my $self = shift;
    ## This test requires RQG_MYSQL_BASE to point to a MySQL installation (or in-source build)
    if ($ENV{RQG_MYSQL_BASE}) {
        # We specify --sqltrace without a value. This should result in all statements
        # being traced, without marking invalid queries.
        my $cmd = $self->{rqg_command_runall_new}.' --mtr-build-thread='.$self->{pb}
                .' --sqltrace > '.$self->{logfile}.' 2>&1';
        $self->annotate("RQG command line: $cmd");
        my $status = system($cmd);
        my $expected = STATUS_OK;
        my $actual = $status >> 8;
        $self->assert_num_equals($expected, $actual, 
            "Wrong exit status from runall-new.pl, expected $expected and got $actual");
        
        # Read log and count lines starting with "SELECT pk".
        # We expect 4 lines on basis of test grammar producing dual statements
        # and --queries=2
        my $expected = 4;
        my $lines = 0;
        open(LOGFILE, "<", $self->{logfile}) or $self->assert("Unable to read log file ".$self->{logfile});
        while (my $line = <LOGFILE>) {
            $lines++ if $line =~ m{^\s*SELECT pk};
        }
        close(LOGFILE);
        $self->assert_num_equals($expected, $lines, 
                "Unexpected number of traced statements: Expected $expected, got $lines");
    }
}

sub test_sqltrace_markerrors_runall {
    my $self = shift;
    ## This test requires RQG_MYSQL_BASE to point to a MySQL installation (or in-source build)
    if ($ENV{RQG_MYSQL_BASE}) {
        my $cmd = $self->{rqg_command_runall}.' --mtr-build-thread='.$self->{pb}
                .' --sqltrace=MarkErrors > '.$self->{logfile}.' 2>&1';
        $self->annotate("RQG command line: $cmd");
        my $status = system($cmd);
        my $expected = STATUS_OK;
        my $actual = $status >> 8;
        $self->assert_num_equals($expected, $actual, 
            "Wrong exit status from runall.pl, expected $expected and got $actual");
        
        # Read log and count traced lines.
        # We expect 4 lines on basis of test grammar producing dual statements
        # and --queries=2.
        # 2 statements should have failed, so we expect 2 lines with prefix.
        # 2 statements should have succeeded, so we expect 2 lines with no prefix.
        my $expected_prefixed = 2;
        my $expected_clean = 2;
        my $lines_prefixed = 0;
        my $lines_clean = 0;
        my $prefix_pattern = $self->{trace_prefix_pattern};
        open(LOGFILE, "<", $self->{logfile}) or $self->assert("Unable to read log file ".$self->{logfile});
        while (my $line = <LOGFILE>) {
            $lines_prefixed++  if $line =~ m{$prefix_pattern\s+SELECT pk};
            $lines_clean++ if $line =~ m{^\s*SELECT pk};
        }
        close(LOGFILE);
        $self->assert_num_equals($expected_clean, $lines_clean, 
                "Unexpected number of prefixed (invalid) statements: Expected $expected_clean, got $lines_clean");
        $self->assert_num_equals($expected_prefixed, $lines_prefixed, 
                "Unexpected number of prefixed (invalid) statements: Expected $expected_prefixed, got $lines_prefixed");
    }
}

sub test_sqltrace_markerrors_runall_new {
    my $self = shift;
    ## This test requires RQG_MYSQL_BASE to point to a MySQL installation (or in-source build)
    if ($ENV{RQG_MYSQL_BASE}) {
        my $cmd = $self->{rqg_command_runall_new}.' --mtr-build-thread='.$self->{pb}
                .' --sqltrace=MarkErrors > '.$self->{logfile}.' 2>&1';
        $self->annotate("RQG command line: $cmd");
        my $status = system($cmd);
        my $expected = STATUS_OK;
        my $actual = $status >> 8;
        $self->assert_num_equals($expected, $actual, 
            "Wrong exit status from runall-new.pl, expected $expected and got $actual");
        
        # Read log and count traced lines.
        # We expect 4 lines on basis of test grammar producing dual statements
        # and --queries=2.
        # 2 statements should have failed, so we expect 2 lines with prefix.
        # 2 statements should have succeeded, so we expect 2 lines with no prefix.
        my $expected_prefixed = 2;
        my $expected_clean = 2;
        my $lines_prefixed = 0;
        my $lines_clean = 0;
        my $prefix_pattern = $self->{trace_prefix_pattern};
        open(LOGFILE, "<", $self->{logfile}) or $self->assert("Unable to read log file ".$self->{logfile});
        while (my $line = <LOGFILE>) {
            $lines_prefixed++  if $line =~ m{$prefix_pattern\s+SELECT pk};
            $lines_clean++ if $line =~ m{^\s*SELECT pk};
        }
        close(LOGFILE);
        $self->assert_num_equals($expected_clean, $lines_clean, 
                "Unexpected number of prefixed (invalid) statements: Expected $expected_clean, got $lines_clean");
        $self->assert_num_equals($expected_prefixed, $lines_prefixed, 
              "Unexpected number of prefixed (invalid) statements: Expected $expected_prefixed, got $lines_prefixed");
    }
}

sub test_sqltrace_invalid_value_runall {
    my $self = shift;
    ## This test requires RQG_MYSQL_BASE to point to a MySQL installation (or in-source build)
    if ($ENV{RQG_MYSQL_BASE}) {
        my $cmd = $self->{rqg_command_runall}.' --mtr-build-thread='.$self->{pb}
                .' --sqltrace=invalidValue > '.$self->{logfile}.' 2>&1';
        $self->annotate("RQG command line: $cmd");
        my $status = system($cmd);
        my $not_expected = STATUS_OK;
        my $actual = $status >> 8;
        $self->assert_num_not_equals($not_expected, $actual, 
            "Wrong exit status from runall.pl, did not expect $not_expected");
    }
}

sub test_sqltrace_invalid_value_runall_new {
    my $self = shift;
    ## This test requires RQG_MYSQL_BASE to point to a MySQL installation (or in-source build)
    if ($ENV{RQG_MYSQL_BASE}) {
        my $cmd = $self->{rqg_command_runall_new}.' --mtr-build-thread='.$self->{pb}
                .' --sqltrace=invalidValue > '.$self->{logfile}.' 2>&1';
        $self->annotate("RQG command line: $cmd");
        my $status = system($cmd);
        my $not_expected = STATUS_OK;
        my $actual = $status >> 8;
        $self->assert_num_not_equals($not_expected, $actual, 
            "Wrong exit status from runall-new.pl, did not expect $not_expected");
    }
}

1;
