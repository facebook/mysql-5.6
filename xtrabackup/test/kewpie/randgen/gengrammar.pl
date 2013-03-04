#!/usr/bin/perl

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
use lib "$ENV{RQG_HOME}/lib";
use strict;

use GenTest;
use GenTest::Constants;
use GenTest::Grammar;

use Getopt::Long;

$| = 1;

my ($grammar_file, $mask, $mask_level, $thread_id, $help);

my @ARGV_saved = @ARGV;

my $opt_result = GetOptions(
	'grammar=s' => \$grammar_file,
	'mask=i' => \$mask,
	'mask-level=i' => \$mask_level,
	'thread-id=i' => \$thread_id
);

help() if !$opt_result || $help || not defined $grammar_file;

my $grammar = GenTest::Grammar->new( grammar_file => $grammar_file );

my $top_grammar = ($mask_level > 0) ? $grammar->topGrammar($mask_level, "thread".$thread_id, "query") : $grammar;

if ($mask > 0) {
	my $masked_grammar = $top_grammar->mask($mask);
	$grammar = $grammar->patch($masked_grammar);
}

print $grammar->toString();

exit(0);

sub help {
        print <<EOF
$0 - Dump a grammar after applying masking

        --grammar   : Grammar file to use for generating the queries (REQUIRED);
        --mask      : A seed to a random mask used to mask (reeduce) the grammar.
        --mask-level: How many levels deep the mask is applied (default 1)
        --help      : This help message
EOF
        ;
	exit(1);
}

