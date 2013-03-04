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

use lib 'lib';

use GenTest;
use GenTest::Executor;
use GenTest::App::Gendata;
use GenTest::Grammar;
use GenTest::Generator::FromGrammar;
use Time::HiRes;

my $arg = shift;

if ($arg eq 'grammar') {
########################################
    say("Benchmark $arg: FromGrammar->next()");
    
    my $grammar = GenTest::Grammar->new(grammar_file => 'bench/WL5004_sql.yy');
    
    my $generator = GenTest::Generator::FromGrammar->new(grammar => $grammar,
                                                         seed => 0);
    
    my $executor = GenTest::Executor->newFromDSN("dummy");
    $executor->init();
    $executor->cacheMetaData();
    
    my $start = Time::HiRes::time();
    foreach $i (0..50000) {
    $generator->next([$executor,undef]);
    }
    my $stop = Time::HiRes::time();
    open TM,">bench/grammar.dat";
    print TM "YVALUE = ".($stop - $start)."\n";
    close TM;
    
} elsif ($arg eq 'gendata') {
########################################
    say("benchmark $arg: Gendata->run()");
    
    my $gen = GenTest::App::Gendata->new(dsn => "dummy",
                                         spec_file => "bench/falcon_data_types.zz",
                                         rows => 5000,
                                         views => 1);
    
    
    my $start = Time::HiRes::time();
    my $status = $gen->run();
    my $stop = Time::HiRes::time();
    
    open TM,">bench/gendata.dat";
    print TM "YVALUE = ".($stop - $start)."\n";
    close TM;
} else {
    foreach my $b ('gendata','grammar') {
        say("----------------------------------------------");
        system("perl -d:DProf bench/benchmark.pl $b");
        say("Collecting profiling information");
        system("dprofpp -u");
    }
}
