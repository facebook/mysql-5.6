#!/usr/bin/perl

use strict;
use lib 'lib';
use lib '../lib';

use Data::Dumper;
use Getopt::Long;

use GenTest;
use GenTest::Generator::FromDirectory;
use GenTest::QueryPerformance;
use GenTest::QueryPerformanceDelta;
use GenTest::Executor;
use GenTest::Executor::MySQL;
use GenTest::Constants;
use GenTest::Comparator;

$| = 1;

my (@dsns, $filter, $in_dir_name, $out_file_name);

say("Please see http://forge.mysql.com/wiki/Category:RandomQueryGenerator for more information on this test framework.");
say("Starting $0 ".join(" ", @ARGV));

my $opt_result = GetOptions(
	'dsn1=s'		=> \$dsns[0],
        'dsn2=s'		=> \$dsns[1],
	'input-directory=s'	=> \$in_dir_name,
	'output-file=s'		=> \$out_file_name,
	'filter=s'		=> \$filter
);

if ($opt_result == 0) {
	exit(STATUS_ENVIRONMENT_FAILURE);
} elsif (not (defined $dsns[0] && defined $dsns[1] && defined $in_dir_name)) {
	say("The following options are required: --dsn1 , --dsn2 , --input-directory");
	exit(STATUS_ENVIRONMENT_FAILURE);
}

if (defined $out_file_name) {
	open (OUT_FILE, ">$out_file_name") or die "Unable to open output file $out_file_name: $!";
	say("Dumping results to $out_file_name.");
	select OUT_FILE; $| = 1; select STDOUT;
}

if (not defined $filter) {
	say("No filter defined. All results will be dumped.");
} else {
	say("Filter: $filter") if defined $filter;
}

my @executors;

foreach my $server_id (0..1) {
	say("Connecting to server at DSN $dsns[$server_id] ...");
	my $executor = GenTest::Executor->newFromDSN($dsns[$server_id]);

	exit(STATUS_ENVIRONMENT_FAILURE) if $executor->init() != STATUS_OK;

#	if (!$executor->read_only()) {
#		say("Executor for dsn: ".$executor->dsn()." has been granted more than just SELECT privilege. Please restrict the user and try again");
#		exit(STATUS_ENVIRONMENT_FAILURE);
#	} else {
		$executor->setFlags($executor->flags() | EXECUTOR_FLAG_PERFORMANCE | EXECUTOR_FLAG_HASH_DATA );
#	}

#	$executor->execute("SET GLOBAL innodb_stats_sample_pages = 128");
#	$executor->execute("SHOW TABLE STATUS");

	$executors[$server_id] = $executor;
	say("... done.");
}

my $generator = GenTest::Generator::FromDirectory->new( directory_name => $in_dir_name );
my %counters = (
	incoming_queries	=> 0,
	executed_queries	=> 0,
	reported_queries	=> 0,
	error_queries		=> 0,
	diverging_queries	=> 0
);

query: while (my $query_ref = $generator->next(\@executors)) {
	$counters{incoming_queries}++;
	last if $query_ref == STATUS_EOF;
	my $query = $query_ref->[0];
	next if $query !~ m{\s*SELECT}sgio;
	$counters{executed_queries}++;
	
	foreach my $temperature ('cold','warm') {
		my @results;
		foreach my $server_id (0..1) {
			$results[$server_id] = $executors[$server_id]->execute($query);
		}

		if (($results[0]->status() != STATUS_OK) || ($results[1]->status() != STATUS_OK)) {
			$counters{error_queries}++;
			next query;
		} elsif (GenTest::Comparator::compare($results[0], $results[1]) != STATUS_OK) {
	                say("The two servers returned different result sets for query: $query ;");
			$counters{diverging_queries}++;
			next query;
		}

		my $performance_delta = GenTest::QueryPerformanceDelta->new(
			query => $query,
			temperature => $temperature,
			performances => [ $results[0]->performance() , $results[1]->performance() ]
		);

		if ($performance_delta->matchesFilter($filter) == STATUS_OK) {
			$counters{reported_queries}++;
			print $performance_delta->toString();
			print OUT_FILE $performance_delta->serialize() if defined $out_file_name;
		}
	}
}

say("Run statistics:");
say("Incoming: $counters{incoming_queries} queries.");
say("Executed: $counters{executed_queries} queries.");
say("Reported: $counters{reported_queries} queries.");
say();
say("Errors: $counters{error_queries} queries.");
say("Diverging results: $counters{diverging_queries} queries.");

close OUT_FILE if defined $out_file_name;
