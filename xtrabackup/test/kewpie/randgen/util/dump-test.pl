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

use strict;
use lib 'lib';
use lib '../lib';
use Carp;
use DBI;
use Getopt::Long;

use GenTest::Properties;
use GenTest::Constants;
use GenTest::Executor::MySQL;
use GenTest::Simplifier::SQL;
use GenTest::Simplifier::Test;


my $options = {};
GetOptions($options,
           'config=s',
           'dsn=s@',
           'query=s');
my $config = GenTest::Properties->new(
    options => $options,
    defaults => {dsn=>['dbi:mysql:host=127.0.0.1:port=19306:user=root:database=test', 
                       'dbi:mysql:host=127.0.0.1:port=19308:user=root:database=test']},
    required => ['dsn','query']);


my @executors;
my @results;

foreach my $dsn (@{$config->dsn}) {
    print "....$dsn\n";
	my $executor = GenTest::Executor->newFromDSN($dsn);
	$executor->init();
	push @executors, $executor;

	my $result = $executor->execute($config->query, 1);
	push @results, $result;
}

my $simplifier_test = GenTest::Simplifier::Test->new(
	executors => \@executors,
	queries => [ $config->query ],
	results => [ \@results ]
);

my $test = $simplifier_test->simplify();

print $test;
