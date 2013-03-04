# Copyright (C) 2009 Sun Microsystems, Inc. All rights reserved.  Use
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

# Basic grammar test
# Walk through all the grammars and feed them to the Grammar
# constructor. 
#
package ParseAllGrammars;
use base qw(Test::Unit::TestCase);
use lib 'lib';
use GenTest::Grammar;
use GenTest::Executor;
use GenTest::Executor::Dummy;
use GenTest::Generator::FromGrammar;
use File::Find;
use Digest::SHA1  qw(sha1_base64);

use Data::Dumper;

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
}

my @files;
sub isGrammar {
    if ($File::Find::name =~ m/\.yy$/) {
	push(@files, $File::Find::name);
    }
}

sub test_parse {
    my $self = shift;
    @files = ();
    find(\&isGrammar, ("conf"));
    my @filesToTest = sort(@files);
    if (defined $ENV{RQG_OTHER_GRAMMAR_DIR}) {
	@files = ();
	find(\&isGrammar, ($ENV{RQG_OTHER_GRAMMAR_DIR}));
	@filesToTest = (@filesToTest, sort(@files));
    }
    #@files = sort((@files));

    foreach $f (@filesToTest) {
        my $grammar = new GenTest::Grammar(grammar_file => $f);
        $self->assert_not_null($grammar, "Grammar was null: $f");

        # Skip further checks of redefine files, recognized by "_redefine." in 
        # file name. These files can contain just a subset of a grammar, so 
        # there is no point looking for a specific grammar rule.
        my $redefine_file = undef;
        if ($f =~ m{_redefine\.}) {
            $redefine_file = 1;
        }

        if (!defined $redefine_file) {
            print "... $f\n";
            my $startRule = $grammar->firstMatchingRule("query");
            $self->assert_not_null($startRule, '"query" rule was null in '.$f);
            my $executor = GenTest::Executor->newFromDSN("dummy");
            $self->assert_not_null($executor);
            $executor->init();
            $executor->cacheMetaData();
            my $generator = GenTest::Generator::FromGrammar->new(
                grammar_file => $f,
                seed => 0
                );
            $self->assert_not_null($generator);
            foreach $i (1..100) {
                $generator->next([$executor,undef]);
            }

        } else {
            print ".*. $f\n";
        }
    }

    sub isStable {
        my ($self, $file, $hash) = @_;
        my $executor = GenTest::Executor->newFromDSN("dummy");
        $self->assert_not_null($executor);
        $executor->init();
        $executor->cacheMetaData();
        my $generator = GenTest::Generator::FromGrammar->new(
            grammar_file => $file,
            seed => 2
            );
        
        $self->assert_not_null($generator);
        my $data;
        foreach $i (1..1000) {
            $data .= $generator->next([$executor,undef])->[0];
        }
        my $newhash =sha1_base64($data);
        $self->assert_str_equals($newhash,$hash,"'$newhash' different from '$hash'. Generator or grammar changed for $file");
        
    }
    
    sub test_stability {
        ## Test stability of selected grammars
        my ($self) = @_;
        $self->isStable("conf/examples/example.yy", 
                        "GVRJcbOKijEUdJQjuor4upMdzDo");
   } 
}


1;
