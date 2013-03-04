#!/usr/bin/perl

use strict;
use lib 'lib';
use lib '../lib';

use English '-no_match_vars';
use Getopt::Long;

use GenTest;
use GenTest::QueryPerformance;
use GenTest::QueryPerformanceDelta;
use GenTest::Constants;

my ($in_file_name, $filter);

say("Please see http://forge.mysql.com/wiki/Category:RandomQueryGenerator for more information on this test framework.");
say("Starting $0 ".join(" ", @ARGV));

my $opt_result = GetOptions(
	'input-file=s'	=> \$in_file_name,
	'filter=s'	=> \$filter
);

if ($opt_result == 0) {
	exit(STATUS_ENVIRONMENT_FAILURE);
} elsif (not defined $in_file_name) {
        say("The following option is required: --input-file");
        exit(STATUS_ENVIRONMENT_FAILURE);
}

say("Filter: $filter") if defined $filter;

my %counters = (
	incoming_queries	=> 0,
	reported_queries	=> 0
);

open (IN_FILE, "<$in_file_name") or die "Unable to open input file $in_file_name: $!";
$INPUT_RECORD_SEPARATOR = " ]]>\n";

while (<IN_FILE>) {
	$_ =~ s{^<!\[CDATA\[ }{}sgio;
	chomp($_);
	
	my $VAR1;
	my $performance_delta = eval($_);
	die("Error reading $in_file_name: $@") if $@ ne '';

	$counters{incoming_queries}++;

	if ($performance_delta->matchesFilter($filter) == STATUS_OK) {
		$counters{reported_queries}++;
		print $performance_delta->toString();
	}
}

say("Run statistics:");
say("Incoming: $counters{incoming_queries} queries.");
say("Reported: $counters{reported_queries} queries.");

close IN_FILE;
