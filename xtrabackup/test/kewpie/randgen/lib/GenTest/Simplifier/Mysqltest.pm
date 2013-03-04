# Copyright (c) 2008,2010 Oracle and/or its affiliates. All rights reserved.
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

package GenTest::Simplifier::Mysqltest;

require Exporter;
use GenTest;
@ISA = qw(GenTest);

use strict;
use lib 'lib';
use GenTest;
use GenTest::Constants;

my @csv_modules = (
	'Text::CSV',
	'Text::CSV_XS',
	'Text::CSV_PP'
);

use constant SIMPLIFIER_ORACLE          => 0;
use constant SIMPLIFIER_FILTER		=> 1;
use constant SIMPLIFIER_USE_CONNECTIONS	=> 2;

1;

sub new {
        my $class = shift;

	my $simplifier = $class->SUPER::new({
		oracle		=> SIMPLIFIER_ORACLE,
		filter		=> SIMPLIFIER_FILTER,
		use_connections => SIMPLIFIER_USE_CONNECTIONS
	}, @_);

	return $simplifier;
}

sub simplify {
	my ($simplifier, $initial_mysqltest) = @_;

        my @queries_filtered;

	if (defined $simplifier->[SIMPLIFIER_FILTER]) {
	        my $filter = $simplifier->[SIMPLIFIER_FILTER];
		foreach (split("\n", $initial_mysqltest)) {
			push @queries_filtered, $_ if $_ !~ m{$filter}sio;
		}
	} else {
		@queries_filtered = split("\n", $initial_mysqltest);
	}

        say(($#queries_filtered + 1)." queries remain after filtering.");

	if ($simplifier->oracle(join("\n", @queries_filtered)."\n") == ORACLE_ISSUE_NO_LONGER_REPEATABLE) {
		warn("Initial mysqltest (after filtering) failed oracle check.");
		return undef;
        }

	my $ddmin_outcome = $simplifier->ddmin(\@queries_filtered);
	my $final_mysqltest = join("\n", @$ddmin_outcome)."\n";

	if ($simplifier->oracle($final_mysqltest) == ORACLE_ISSUE_NO_LONGER_REPEATABLE) {
		warn("Final mysqltest failed oracle check.");
		return undef;
	} else {
		return $final_mysqltest;
	}
}

sub simplifyFromCSV {
	my ($simplifier, $csv_file) = @_;

	my $csv;
	foreach my $csv_module (@csv_modules) {
	        eval ("require $csv_module");
		if (!$@) {
			$csv = $csv_module->new({ 'escape_char' => '\\' });
			say("Loaded CSV module $csv_module");
			last;
		}
	}

	die "Unable to load a CSV Perl module" if not defined $csv;

	my @mysqltest;
	
	open (CSV_HANDLE, "<", $csv_file) or die $!;
	my %connections;
	my $last_connection;
	while (<CSV_HANDLE>) {
		$_ =~ s{\\n}{ }sgio;
		if ($csv->parse($_)) {
			my @columns = $csv->fields();
			my $connection_id = $columns[2];
			my $connection_name = 'connection_'.$connection_id;
			my $command = $columns[4];
			my $query = $columns[5];

			if (($command eq 'Connect') && ($simplifier->[SIMPLIFIER_USE_CONNECTIONS])) {
				my ($username, $host, $database) = $query =~ m{(.*?)\@(.*?) on (.*)}sio;
				push @mysqltest, "--connect ($connection_name, localhost, $username, , $database)";
				$connections{$connection_name}++;
			} elsif (($command eq 'Quit') && ($simplifier->[SIMPLIFIER_USE_CONNECTIONS])) {
				push @mysqltest, "--disconnect $connection_name";
         } elsif ($command eq 'Query' or $command eq 'Init DB') {
				if (($last_connection ne $connection_name) && ($simplifier->[SIMPLIFIER_USE_CONNECTIONS])) {
					if (not exists $connections{$connection_name}) {
						push @mysqltest, "--connect ($connection_name, localhost, root, , test)";
		                                $connections{$connection_name}++;
					}

					push @mysqltest, "--connection $connection_name";
					$last_connection = $connection_name;
				}
			
				$query =~ s{\\n}{ }sgio;
				$query =~ s{\\\\}{\\}sgio;

            if ($command eq 'Init DB') {
               # mysqldump causes entries like
               #    ...,"root[root] @ localhost [127.0.0.1]",17,1,"Init DB","test1"
               # which seem to change the default database to the database named at the end of the line.
               # Replace this by USE <database>
               push @mysqltest, ('USE '.$query.';');
            } else {
				   if ($query =~ m{;}){
					   push @mysqltest, ("DELIMITER |;",$query.'|', "DELIMITER ;|");
				   } else {
					   push @mysqltest, $query.';';
				   }
            }
			}
	   } else {
			my $err = $csv->error_input;
			say ("Failed to parse line: $err");
		}
	}
	close CSV_HANDLE;

	say("Loaded ".($#mysqltest + 1)." lines from CSV");

	return $simplifier->simplify(join("\n", @mysqltest)."\n");
}

sub oracle {
        my ($simplifier, $mysqltest) = @_;

        my $oracle = $simplifier->[SIMPLIFIER_ORACLE];

	return $oracle->($mysqltest); 
}

#
# This is an implementation of the ddmin algorithm, as described in "Why Programs Fail" by Andreas Zeller
#

sub ddmin {
	my ($simplifier, $inputs) = @_;
	say("input_size: ".($#$inputs + 1));
	my $splits = 2;

	# We start from 1, as to preserve the top-most queries since they are usually vital
	my $starting_subset = 1;

	outer: while (2 <= @$inputs) {
		my @subsets = subsets($inputs, $splits);
		say("inputs: ".($#$inputs + 1)."; splits: $splits; subsets: ".($#subsets + 1));

		my $some_complement_is_failing = 0;
		foreach my $subset_id ($starting_subset..$#subsets) {
			my $subset = $subsets[$subset_id];
			my $complement = listMinus($inputs, $subset);
			say("subset_id: $subset_id; subset_size: ".($#$subset + 1)."; complement_size: ".($#$complement + 1));
#			say("subset: ".join('|',@$subset));
#			say("complement: ".join('|',@$complement));
			if ($simplifier->oracle(join("\n", @$complement)) == ORACLE_ISSUE_STILL_REPEATABLE) {
				$starting_subset = $subset_id; 	# At next iteration, continue from where we left off 
				$inputs = $complement;
				$splits-- if $splits > 2;
				$some_complement_is_failing = 1;
				next outer;
			}
		}

		if (!$some_complement_is_failing) {
			last if $splits == ($#$inputs + 1);
			$splits = $splits * 2 > $#$inputs + 1 ? $#$inputs + 1 : $splits * 2;
		}

		$starting_subset = 1;	# Reached EOF, start again from the top

	}

	return $inputs;
}

sub subsets {
	my ($list1, $subset_count) = @_;

	my $subset_size = int(($#$list1 + 1) / $subset_count);

	my @subsets;
	my $current_subset = 0;
	foreach my $element_id (0..$#$list1) {
		push @{$subsets[$current_subset]}, $list1->[$element_id];
		$current_subset++ if ($#{$subsets[$current_subset]} + 1) >= $subset_size && ($current_subset + 1) < $subset_count;
	}

	return @subsets;
}

sub listMinus {
	my ($list1, $list2) = @_;

	my $list1_string = join("\n", @$list1);
	my $list2_string = join("\n", @$list2);
	
	my $list3_string = $list1_string;
	my $list2_pos = index($list1_string, $list2_string);
	if ($list2_pos > -1) {
		substr($list3_string, $list2_pos, length($list2_string), '');
		$list3_string =~ s{^\n}{}sgio;
		$list3_string =~ s{\n$}{}sgio;
		my @list3 = split (m{\n+}, $list3_string);
		return \@list3;
	} else {
		die "list2 is not a subset of list1";
	}
}	

1;
