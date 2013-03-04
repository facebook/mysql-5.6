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

# Do a simple run of scripts to see that they're sound
#
package TestTT;
use base qw(Test::Unit::TestCase);
use lib 'lib';
use GenTest;
use Cwd;

sub new {
    my $self = shift()->SUPER::new(@_);
    # your state for fixture here
    return $self;
}

my $generator;
sub set_up {
}

sub tear_down {
    # clean up after test
    unlink "unit/test1.xml";
    unlink "unit/test2.xml";
    unlink "unit/foo1.log";
    unlink "unit/foo2.log";
    system("rm -r unit/example*");
}

sub test_xml_runall {
    my $portbase = $ENV{TEST_PORTBASE}>0?int($ENV{TEST_PORTBASE}):22120;
    my $pb = int(($portbase - 10000) / 10);
    my $self = shift;
    ## This test requires RQG_MYSQL_BASE to point to a in source Mysql database
    if ($ENV{RQG_MYSQL_BASE}) {
        $ENV{LD_LIBRARY_PATH}=join(":",map{"$ENV{RQG_MYSQL_BASE}".$_}("/libmysql/.libs","/libmysql","/lib/mysql"));
        my $status = system("perl -MCarp=verbose ./runall.pl --mtr-build-thread=$pb --grammar=conf/examples/example.yy --gendata=conf/examples/example.zz --queries=3 --threads=3 --report-xml-tt --report-xml-tt-type=none  --xml-output=unit/test1.xml --logfile=unit/foo1.log --report-tt-logdir=unit --basedir=".$ENV{RQG_MYSQL_BASE});
        $self->assert_equals(0, $status);
    }
}

sub test_xml_runall_new {
    my $self = shift;
    ## This test requires RQG_MYSQL_BASE to point to a Mysql database (in source, out of source or installed)
    my $portbase = 10 + ($ENV{TEST_PORTBASE}>0?int($ENV{TEST_PORTBASE}):22120);
    my $pb = int(($portbase - 10000) / 10);

    
    if ($ENV{RQG_MYSQL_BASE}) {
        $ENV{LD_LIBRARY_PATH}=join(":",map{"$ENV{RQG_MYSQL_BASE}".$_}("/libmysql/.libs","/libmysql","/lib/mysql"));
        my $status = system("perl -MCarp=verbose ./runall-new.pl --mtr-build-thread=$pb --grammar=conf/examples/example.yy --gendata=conf/examples/example.zz --queries=3 --threads=3 --report-xml-tt --report-xml-tt-type=none --xml-output=unit/test2.xml --logfile=unit/foo2.log --report-tt-logdir=unit --basedir=".$ENV{RQG_MYSQL_BASE}." --vardir=".cwd()."/unit/tmp");
        $self->assert_equals(0, $status);
    }
}

1;
