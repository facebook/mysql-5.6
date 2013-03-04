#!/usr/bin/perl

# Copyright (C) 2008-2009 Sun Microsystems, Inc. All rights reserved.
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

use lib 'lib';
use lib "$ENV{RQG_HOME}/lib";
use strict;

use GenTest;
use GenTest::Constants;
use GenTest::Properties;
use GenTest::Generator::FromGrammar;
use GenTest::Executor;
use Getopt::Long;

my $DEFAULT_QUERIES = 1000;

my @ARGV_saved = @ARGV;
my $options = {};
my $opt_result = GetOptions($options,
			    'config=s',
			    'grammar=s',
			    'queries=i',
			    'help',
			    'seed=s',
			    'mask=i',
			    'mask-level=i',
			    'dsn=s');

help() if !$opt_result;

my $config = GenTest::Properties->new(options => $options,
				      defaults => {seed => 1, 
						   queries=> $DEFAULT_QUERIES},
				      legal => ['config',
						'queries',
						'help',
						'seed',
						'mask',
						'mask-level',
						'dsn'],
				      required => ['grammar'],
				      help => \&help);

my $seed = $config->seed;
if ($seed eq 'time') {
    $seed = time();
    say("Converting --seed=time to --seed=$seed");
}

my $generator = GenTest::Generator::FromGrammar->new(
    grammar_file => $config->grammar,
    seed => $seed,
    mask => $config->mask,
    mask_level => $config->property('mask-level')
    );

return STATUS_ENVIRONMENT_FAILURE if not defined $generator;

my $executor;

if (defined $config->dsn) {
    $executor = GenTest::Executor->newFromDSN($config->dsn);
    exit (STATUS_ENVIRONMENT_FAILURE) if not defined $executor;
}

if (defined $executor) {
    my $init_result = $executor->init();
    exit ($init_result) if $init_result > STATUS_OK;
    $executor->cacheMetaData();
}

foreach my $i (1..$config->queries) {
    my $queries = $generator->next([$executor]);
    if (
	(not defined $queries) ||
	($queries->[0] eq '')
	) {
	say("Grammar produced an empty query. Terminating.");
	exit(STATUS_ENVIRONMENT_FAILURE);
    }
    my $sql = join('; ',@$queries);
    print $sql.";\n";
}

exit(0);

sub help {
    print <<EOF
$0 - Generate random queries from an SQL grammar and pipe them to STDOUT

        --grammar   : Grammar file to use for generating the queries (REQUIRED);
        --seed      : Seed for the pseudo-random generator
        --queries   : Numer of queries to generate (default $DEFAULT_QUERIES);
        --dsn       : The DSN of the database that will be used to resolve rules such as _table , _field
        --mask      : A seed to a random mask used to mask (reeduce) the grammar.
        --mask-level: How many levels deep the mask is applied (default 1)
        --help      : This help message
EOF
        ;
    exit(1);
}

