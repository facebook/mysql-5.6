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

$| = 1;
use strict;
use lib 'lib';
use lib "$ENV{RQG_HOME}/lib";
use DBI;
use Getopt::Long;
use GenTest;
use GenTest::Constants;
use GenTest::App::GendataSimple;

my ($dsn, $engine, $help, $views, $notnull);

my @ARGV_saved = @ARGV;

my $opt_result = GetOptions(
	'dsn=s' => \$dsn,
	'engine:s' => \$engine,
	'help' => \$help,
	'views' => \$views,
	'notnull' => \$notnull
);

my $default_dsn = GenTest::App::GendataSimple->defaultDsn();

help() if !$opt_result || $help;

my $app = GenTest::App::GendataSimple->new(dsn => $dsn,
                                           engine => $engine,
                                           views => $views,
					   notnull => $notnull);

say("Starting \n# $0 \\ \n# ".join(" \\ \n# ", @ARGV_saved));

my $status = $app->run();

exit $status;

sub help {
print <<EOF
$0 - Simple data generator. Options:

    --dsn       : MySQL DBI resource to connect to (default $default_dsn)
    --engine    : Table engine to use when creating tables (default: no ENGINE in CREATE TABLE )
    --views     : Generate views
    --notnull	: Generate all fields with NOT NULL
    --help      : This help message 
EOF
;
	safe_exit(1);
}

