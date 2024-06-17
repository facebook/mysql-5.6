# Copyright (c) 2009, 2012 Oracle and/or its affiliates. All rights reserved.
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

# Do a simple run of scripts to see that they're sound
#
package TestScripts;
use base qw(Test::Unit::TestCase);
use lib 'lib';
use GenTest;
use GenTest::Constants;
use Cwd;
use File::Path qw(rmtree);

sub new {
    my $self = shift()->SUPER::new(@_);
    return $self;
}

my $generator;  
my $counter=0; # to avoid port clashes.

sub set_up {
    my $self=shift;
    # Set temporary working directory. Used for vardir, workdir etc. in tests. 
    # Remove it in tear_down() to avoid interference between tests!
    $self->{workdir} = cwd()."/unit/tmpwd2"; 
    my $portbase = ($counter*10) + ($ENV{TEST_PORTBASE}>0 ? int($ENV{TEST_PORTBASE}) : 22120);
    $self->{portbase} = int(($portbase - 10000) / 10);
    $counter++;
}

sub tear_down {
    my $self = shift;
    # clean up after test:
    
    # Not all tests use the workdir, so we need to check if it exists.
    if (-e $self->{workdir}) {
        rmtree($self->{workdir}) or print("UNABLE TO REMOVE DIR ".$self->{workdir}.": $!\n");
    }
    # Remove replication slave wordir, if it exists.
    if (-e $self->{workdir}."_slave") {
        rmtree($self->{workdir}."_slave") or print("UNABLE TO REMOVE DIR ".$self->{workdir}."_slave".": $!\n");
    }
}

sub test_gensql {
    my $self = shift;

    my $status = system("perl -MCarp=verbose gensql.pl --grammar=conf/examples/example.yy --dsn=dummy --queries=1");

    $self->assert_equals(0, $status);

    my $status = system("perl -MCarp=verbose gensql.pl --grammar=unit/testStack.yy --dsn=dummy --queries=5");

    $self->assert_equals(0, $status);

}

sub test_gendata {
    my $self = shift;

    my $status = system("perl -MCarp=verbose gendata.pl --spec=conf/examples/example.zz --dsn=dummy");

    $self->assert_equals(0, $status);
}

sub test_gendata_old {
    my $self = shift;

    my $status = system("perl -MCarp=verbose gendata-old.pl --dsn=dummy");

    $self->assert_equals(0, $status);
}

sub test_gentest {
    my $self = shift;

    my $status = system("perl -MCarp=verbose gentest.pl --dsn=dummy --grammar=conf/examples/example.yy --threads=1 --queries=1");

    $self->assert_equals(0, $status);

    $status = system("perl -MCarp=verbose gentest.pl --dsn=dummy --grammar=conf/examples/example.yy --threads=1 --queries=1 --mask=10 --mask-level=2");

    $self->assert_equals(0, $status);
}

sub test_runall {
    ##if ($ENV{TEST_OUT_OF_SOURCE}) {
    ##    ## runall does not work with out of source builds
    ##    say("test_runall skipped for out-of-source build");
    ##    return;
    ##}

    if ($ENV{TEST_SKIP_RUNALL}) {
        say((caller(0))[3].": Skipping runall.pl test");
        return;
    }

    my $self = shift;
    ## This test requires RQG_MYSQL_BASE to point to a in source Mysql database
    if ($ENV{RQG_MYSQL_BASE}) {
        $ENV{LD_LIBRARY_PATH}=join(":",map{"$ENV{RQG_MYSQL_BASE}".$_}("/libmysql/.libs","/libmysql","/lib/mysql"));
        $ENV{MTR_BUILD_THREAD}=$self->{portbase};
        my $status = system("perl -MCarp=verbose ./runall.pl --grammar=conf/examples/example.yy --gendata=conf/examples/example.zz --queries=3 --threads=3 --reporter=Shutdown --basedir=".$ENV{RQG_MYSQL_BASE}." --vardir=".$self->{workdir});
        $self->assert_equals(0, $status);
    }
}

sub test_runall_new {
    my $self = shift;
    ## This test requires RQG_MYSQL_BASE to point to a Mysql database (in source, out of source or installed)
    if ($ENV{RQG_MYSQL_BASE}) {
        $ENV{LD_LIBRARY_PATH}=join(":",map{"$ENV{RQG_MYSQL_BASE}".$_}("/libmysql/.libs","/libmysql","/lib/mysql"));
        $ENV{MTR_BUILD_THREAD}=$self->{portbase};
        my $status = system("perl -MCarp=verbose ./runall-new.pl --grammar=conf/examples/example.yy --gendata=conf/examples/example.zz --queries=3 --threads=3 --reporter=Shutdown --basedir=".$ENV{RQG_MYSQL_BASE}." --vardir=".$self->{workdir});
        $self->assert_equals(0, $status);
    }
}

## This test is no longer working. Reason:
## MTR v1 gives the same tmp directory for both master and slave
## When 5399
## revid:krunal.bauskar@oracle.com-20130121052701-c57rsp1jsu4u96uw  
## was pushed to trunk, the file $TMPDIR/ibtmp1 is used from both
## slave and master (because they have the same tmp dir), end the
## slave stops due to 
## 2013-01-23 12:51:23 7544 [ERROR] InnoDB: Unable to lock ..../tmp/ibtmp1, error: 11
## Since MTR v1 is no longer maintained, this has no easy fix. And
## since MTR v2 has diverged too much to be a plugin replacement of
## MTR v1. runall.pl no longer supports replication. Note that this
## test may succeed on some computers sincde this is timing related.
# sub test_runall_replication {
#    my $self = shift;
#    if ($ENV{TEST_SKIP_RUNALL}) {
#        say((caller(0))[3].": Skipping runall.pl test");
#        return;
#    }
#    
#    my $pb = $self->{portbase};
#    ## This test requires RQG_MYSQL_BASE to point to a in source Mysql database
#    if ($ENV{RQG_MYSQL_BASE}) {
#        $ENV{LD_LIBRARY_PATH}=join(":",map{"$ENV{RQG_MYSQL_BASE}".$_}("/libmysql/.libs","/libmysql","/lib/mysql"));
#        my $status = system("perl -MCarp=verbose ./runall.pl --rpl_mode=default --mtr-build-thread=$pb --grammar=conf/examples/example.yy --gendata=conf/examples/example.zz --queries=3 --threads=2 --reporter=Shutdown --basedir=".$ENV{RQG_MYSQL_BASE}." --vardir=".$self->{workdir});
#        $self->assert_equals(0, $status);
#    }
#}

sub test_runall_new_replication {
    my $self = shift;
    ## This test requires RQG_MYSQL_BASE to point to a Mysql database (in source, out of source or installed)
    my $pb = $self->{portbase};
    
    
    if ($ENV{RQG_MYSQL_BASE}) {
        $ENV{LD_LIBRARY_PATH}=join(":",map{"$ENV{RQG_MYSQL_BASE}".$_}("/libmysql/.libs","/libmysql","/lib/mysql"));
        my $status = system("perl -MCarp=verbose ./runall-new.pl --rpl_mode=default --mtr-build-thread=$pb --grammar=conf/examples/example.yy --gendata=conf/examples/example.zz --queries=3 --threads=2 --basedir=".$ENV{RQG_MYSQL_BASE}." --vardir=".$self->{workdir});
        $self->assert_equals(0, $status);
    }
}

sub test_runall_comparison {
    my $self = shift;
    ##if ($ENV{TEST_OUT_OF_SOURCE}) {
    ##    ## runall does not work with out of source builds
    ##    say("test_runall skipped for out-of-source build");
    ##    return;
    ##}
    
    if ($ENV{TEST_SKIP_RUNALL}) {
        say((caller(0))[3].": Skipping runall.pl test");
        return;
    }
    
    my $pb = $self->{portbase};
    ## This test requires RQG_MYSQL_BASE to point to a in source Mysql database
    if ($ENV{RQG_MYSQL_BASE}) {
        $ENV{LD_LIBRARY_PATH}=join(":",map{"$ENV{RQG_MYSQL_BASE}".$_}("/libmysql/.libs","/libmysql","/lib/mysql"));
        my $status = system("perl -MCarp=verbose ./runall.pl --mtr-build-thread=$pb --grammar=conf/examples/example.yy --gendata=conf/examples/example.zz --queries=3 --threads=2 --reporter=Shutdown --basedir1=".$ENV{RQG_MYSQL_BASE}." --basedir2=".$ENV{RQG_MYSQL_BASE}." --vardir=".$self->{workdir});
        $self->assert_equals(0, $status);
    }
}

sub test_runall_new_comparison {
    my $self = shift;
    ## This test requires RQG_MYSQL_BASE to point to a Mysql database (in source, out of source or installed)
    my $pb = $self->{portbase};
    
    
    if ($ENV{RQG_MYSQL_BASE}) {
        $ENV{LD_LIBRARY_PATH}=join(":",map{"$ENV{RQG_MYSQL_BASE}".$_}("/libmysql/.libs","/libmysql","/lib/mysql"));
        my $status = system("perl -MCarp=verbose ./runall-new.pl --mtr-build-thread=$pb --grammar=conf/examples/example.yy --gendata=conf/examples/example.zz --queries=3 --threads=2 --basedir1=".$ENV{RQG_MYSQL_BASE}." --basedir2=".$ENV{RQG_MYSQL_BASE}." --vardir=".$self->{workdir});
        $self->assert_equals(0, $status);
    }
}

sub test_combinations_basic {
    my $self = shift;
    ## This test requires RQG_MYSQL_BASE to point to a Mysql database (in source, out of source or installed)
    # Basic run
    if ($ENV{RQG_MYSQL_BASE}) {
        $ENV{LD_LIBRARY_PATH}=join(":",map{"$ENV{RQG_MYSQL_BASE}".$_}("/libmysql/.libs","/libmysql","/lib/mysql"));
        $ENV{MTR_BUILD_THREAD}=$self->{portbase};
        my $status = system("perl -MCarp=verbose ./combinations.pl --new --config=unit/test.cc --trials=2 --basedir=".$ENV{RQG_MYSQL_BASE}." --workdir=".$self->{workdir}." --no-log");
        $self->assert_equals(0, $status);
        $self->assert(-e $self->{workdir}."/trial1.log");
    }
}

sub test_combinations_all_once_parallel {
    my $self = shift;
    ## This test requires RQG_MYSQL_BASE to point to a Mysql database (in source, out of source or installed)
    # Test more options:
    # --run-all-combinations-once + trials
    # --force
    # --parallel=2
    my $expected_status = 0;    # expected exit status
    if ($ENV{RQG_MYSQL_BASE}) {
        $ENV{LD_LIBRARY_PATH}=join(":",map{"$ENV{RQG_MYSQL_BASE}".$_}("/libmysql/.libs","/libmysql","/lib/mysql"));
        $ENV{MTR_BUILD_THREAD}=$self->{portbase};
        my $status = system("perl -MCarp=verbose ./combinations.pl --new --config=unit/test.cc --trials=2 --basedir=".$ENV{RQG_MYSQL_BASE}." --workdir=".$self->{workdir}." --run-all-combinations-once --no-log --parallel=2 --force");
        $status = $status >> 8; # a perl system() thing we need to do
        my $log1 = $self->{workdir}."/trial1.log";
        my $log2 = $self->{workdir}."/trial2.log";
        # In case the test fails, slurp and display trial logs for debugging.
        # Using print instead of $self->annotate for log contents to keep end result summary tidy.
        if ($status != $expected_status) {
            open(LOG1, " < $log1") or $self->annotate("Unable to open trial log file $log1: $!");
            my @log1_contents = <LOG1>;
            close(LOG1);
            print("--------------------\ntrial1 log contents: \n@log1_contents \n");
            open(LOG2, " < $log2") or $self->annotate("Unable to open trial log file $log2: $!");;
            my @log2_contents = <LOG2>;
            close(LOG2);
            print("--------------------\ntrial2 log contents: \n@log2_contents \n");
            print("--------------------\nUnexpected exit status ($status) from combinations.pl. See above for details and trial log contents.\n");
        }
        $self->assert_equals($expected_status, $status);
        $self->assert(-e $log1);
        $self->assert(-e $log2);
    }
}

sub test_combinations_exit_status {
    my $self = shift;
    ## This test requires RQG_MYSQL_BASE to point to a Mysql database (in source, out of source or installed)
    # Test with known failures and check that exit value is the largest of the 
    # exit values of individual runs. Requires special .cc file and 
    # --run-all-combinations-once and no small value for --trials.
    # We use unix cp and sed for now, so avoid running on Windows.
    if ($ENV{RQG_MYSQL_BASE} && not osWindows()) {
        $ENV{LD_LIBRARY_PATH}=join(":",map{"$ENV{RQG_MYSQL_BASE}".$_}("/libmysql/.libs","/libmysql","/lib/mysql"));
        $ENV{MTR_BUILD_THREAD}=$self->{portbase};
        
        # First, we construct a custom "Alarm reporter" which should return STATUS_ALARM when run.
        # This is done by looking for the string "Version:" in the server log.
        my $custom_reporter = "lib/GenTest/Reporter/CustomAlarm.pm";
        my $pre_status = system("cp lib/GenTest/Reporter/ErrorLogAlarm.pm $custom_reporter");
        $self->annotate("Copying of ErrorLogAlarm.pm failed! $!") if $pre_status > 0;
        $pre_status = 0;
        $pre_status = system('sed -i -e \'s/my \$pattern = "\^ERROR"/my $pattern = "^Version:"/\' '.$custom_reporter);
        $pre_status += system('sed -i \'s/package GenTest::Reporter::ErrorLogAlarm;/package GenTest::Reporter::CustomAlarm;/\' '.$custom_reporter);
        $self->annotate("Modification of CustomAlarm.pm failed! $!") if $pre_status > 0;
        # Using exit_status.cc we expect 4 runs, with the following exit statuses in random order:
        #   STATUS_ENVIRONMENT_FAILURE (110)
        #   STATUS_OK (0) (x2)
        #   STATUS_ALARM (109)
        # Total exit status should be the largest of these: 110
        my $status = system("perl -MCarp=verbose ./combinations.pl --new --config=unit/exit_status.cc --basedir=".$ENV{RQG_MYSQL_BASE}." --workdir=".$self->{workdir}." --run-all-combinations-once --no-log --no-mask --parallel=2 --force");
        $status = $status >> 8;
        $self->assert_num_equals(110, $status, "Wrong exit status from combinations.pl: Expected STATUS_ENVIRONMENT_FAILURE (110), but got $status");
        $self->assert(-e $self->{workdir}."/trial1.log");
        $self->assert(-e $self->{workdir}."/trial2.log");
        $self->assert(-e $self->{workdir}."/trial3.log");
        $self->assert(-e $self->{workdir}."/trial4.log");
        unlink($custom_reporter) or $self->assert(0, "Unable to delete $custom_reporter");
    }
}

1;
