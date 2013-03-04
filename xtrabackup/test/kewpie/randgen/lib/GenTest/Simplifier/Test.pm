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

package GenTest::Simplifier::Test;

require Exporter;
use GenTest;
@ISA = qw(GenTest);

use strict;

use lib 'lib';
use GenTest::Simplifier::Tables;
use GenTest::Comparator;
use GenTest::Constants;

# Check if SQL::Beautify module is present for pretty printing.
my $pretty_sql;
eval
{
    require SQL::Beautify;
    $pretty_sql = SQL::Beautify->new;
};

use constant SIMPLIFIER_EXECUTORS	=> 0;
use constant SIMPLIFIER_QUERIES		=> 1;
use constant SIMPLIFIER_RESULTS		=> 2;


### Add options to this list to include them in the generated test
### cases. It does not matter whether they only applies to certain
### versions, since the non-existing options will be ignored for a
### given server.

my @optimizer_variables = (
	'optimizer_switch',
    'optimizer_use_mrr',
    'optimizer_condition_pushdown',
	'join_cache_level',
	'join_buffer_size',
	'optimizer_join_cache_level',
	'debug'
);

1;

sub new {
	my $class = shift;

	my $simplifier = $class->SUPER::new({
		executors	=> SIMPLIFIER_EXECUTORS,
		results		=> SIMPLIFIER_RESULTS,
		queries		=> SIMPLIFIER_QUERIES
	}, @_);

	return $simplifier;
}

sub _comment {
    my ($inLine,$useHash) = @_;
    ## Neat for MTR and readability of multiline diffs
    my $splitLine = join("\n# ", split("\n", $inLine));
    if ($useHash) {
        return "# " . $splitLine;
    } else {
        return "/* ". $splitLine . " */"; 
    }
}
sub simplify {
	my ($simplifier,$show_index) = @_;

	my $test;

	my $executors = $simplifier->executors();

	my $results = $simplifier->results();
	my $queries = $simplifier->queries();
	my ($foo, $tcp_port) = $executors->[0]->dbh()->selectrow_array("SHOW VARIABLES LIKE 'port'");

    ## We use Hash-comments in an pure MySQL environment due to MTR
    ## limitations
    my $useHash = 1;
	foreach my $i (0,1) {
		if (defined $executors->[$i]) {
            $useHash = 0 if $executors->[$i]->type() != DB_MYSQL;
        }
    }


	# If we have two Executors determine the differences in Optimizer settings and print them as test comments
	# If there is only one executor, dump its settings directly into the test as test queries

	foreach my $i (0,1) {
		if (defined $executors->[$i]) {
			my $version = $executors->[$i]->getName()." ".$executors->[$i]->version();
			$test .= _comment("Server".$i.": $version",$useHash)."\n";
		}
	}
	$test .= "\n";

	if (defined $executors->[1] and $executors->[0]->type() == DB_MYSQL and $executors->[1]->type() == DB_MYSQL) {
		foreach my $optimizer_variable (@optimizer_variables) {
			my @optimizer_values;
			foreach my $i (0..1) {
				my $optimizer_value = $executors->[$i]->dbh()->selectrow_array('SELECT @@'.$optimizer_variable);
                
				$optimizer_value = 'ON' if $optimizer_value == 1 && $optimizer_variable eq 'engine_condition_pushdown';
				$optimizer_values[$i] = $optimizer_value;
			}

            foreach my $i (0..1) {
                if ($optimizer_values[$i] =~ m{^\d+$}) {
                    $test .= _comment("Server $i : SET SESSION $optimizer_variable = $optimizer_values[$i]",$useHash)."\n";
                } elsif (defined $optimizer_values[$i]) {
                    $test .= _comment("Server $i : SET SESSION $optimizer_variable = '$optimizer_values[$i]'",$useHash)."\n";
                }
            }
		}
		$test .= "\n\n";
	} elsif (defined $executors->[0]) {
        $test .= "--disable_abort_on_error\n";
		foreach my $optimizer_variable (@optimizer_variables) {
			my $optimizer_value = $executors->[0]->dbh->selectrow_array('SELECT @@'.$optimizer_variable);
			$optimizer_value = 'ON' if $optimizer_value == 1 && $optimizer_variable eq 'engine_condition_pushdown';
            
			if ($optimizer_value =~ m{^\d+$}) {
				$test .= "SET SESSION $optimizer_variable = $optimizer_value;\n";
			} elsif (defined $optimizer_value) {
                $test .= "SET SESSION $optimizer_variable = '$optimizer_value';\n";
			}
		}
        $test .= "--enable_abort_on_error\n";
		$test .= "\n\n";
	}

	my $query_count = defined $queries ? $#$queries : $#$results;
	
	# Message to indicate pretty printing module is not present for use.
	if (!defined $pretty_sql) {
		say("INFO :: Could not find the module SQL::Beautify to pretty print the query.");
	}	

	foreach my $query_id (0..$query_count) {

		my $original_query;
		if (defined $queries) {
			$original_query = $queries->[$query_id];
		} else {
			$original_query = $results->[$query_id]->[0]->query();
		}

		$test .= _comment("Begin test case for query $query_id",$useHash)."\n\n";

		my $simplified_database = 'query'.$query_id.$$;

		my $tables_simplifier = GenTest::Simplifier::Tables->new(
			dsn		=> $executors->[0]->dsn(),
			orig_database	=> 'test',
			new_database	=> $simplified_database
		);

		my ($participating_tables, $rewritten_query) = $tables_simplifier->simplify($original_query);
		
		if ($#$participating_tables > -1) {
			$test .= "--disable_warnings\n";
			foreach my $tab (@$participating_tables) {
				$test .= "DROP TABLE /*! IF EXISTS */ $tab;\n";
			}
			$test .= "--enable_warnings\n\n"
		}
			
		my $mysqldump_cmd = "mysqldump -uroot --net_buffer_length=4096 --max_allowed_packet=4096 --no-set-names --compact --skip_extended_insert --force --protocol=tcp --port=$tcp_port $simplified_database ";
		$mysqldump_cmd .= join(' ', @$participating_tables) if $#$participating_tables > -1;
		open (MYSQLDUMP, "$mysqldump_cmd|") or say("Unable to run $mysqldump_cmd: $!");
		while (<MYSQLDUMP>) {
			$_ =~ s{,\n}{,}sgio;
			$_ =~ s{\(\n}{(}sgio;
			$_ =~ s{\)\n}{)}sgio;
			$_ =~ s{`([a-zA-Z0-9_]+)`}{$1}sgio;
			next if $_ =~ m{SET \@saved_cs_client}sio;
			next if $_ =~ m{SET character_set_client}sio;
			$test .= $_;
			
		}
		close (MYSQLDUMP);
		
		$test .= "\n\n";
		
		# If show_index variable is defined then SHOW INDEX statement is executed on the list of 
		# participating tables and the output is printed within comments.
		# This was a request from optimizer team, for them to understand under which circumstances a 
		# query result difference or transformation has taken place.
		if (defined $show_index) {
	        	if ($#$participating_tables > -1) {
	        		foreach my $tab (@$participating_tables) {
	        			$test .= "# /* Output of `SHOW INDEX from $tab` for query $query_id:\n";
	        			my $stmt = $executors->[0]->execute("SHOW INDEX from $simplified_database.$tab");
	        			$test .= "# |".join("|",@{$stmt->columnNames()})."|\n";
	        			foreach my $row (@{$stmt->data()}) {
	        				$test .= "# |".join("|", @$row)."|\n";
	        			}
	        			$test .= "# */\n\n";
	        		}
	        	}
	        }
		
		
		# If pretty printing module is available then use it to format the query
		# otherwise use the existing regex pattern to format the query. 
		if ( defined $pretty_sql) {
			$rewritten_query =~ s{\s+}{ }sgio; # Remove extra spaces.
			$rewritten_query =~ s{`}{}sgio; # Remove backquotes.
			$pretty_sql->query($rewritten_query);
			$test .= $pretty_sql->beautify;
			$test .= $test.";\n\n"; # Include the query terminator.
		} else {
			$rewritten_query =~ s{\s+}{ }sgio;
			$rewritten_query =~ s{`}{}sgio;
			$rewritten_query =~ s{\s+\.}{.}sgio;
			$rewritten_query =~ s{\.\s+}{.}sgio;
			$rewritten_query =~ s{(SELECT|LEFT|RIGHT|FROM|WHERE|GROUP\s+BY|ORDER\s+BY|HAVING|LIMIT)}{\n$1}sgio;
			$rewritten_query =~ s{\(}{\n(}sgio;	# Put each set of parenthesis on its own line 
			$rewritten_query =~ s{\)}{)\n}sgio;	# 
			$rewritten_query =~ s{[\r\n]+}{\n}sgio;
			$test .= $rewritten_query.";\n\n"; 		
		}
		
		$test .= "\n\n";
		
		if ($rewritten_query =~ m/^\s*SELECT/) {
			foreach my $ex (0..1) {
				if (defined $executors->[$ex]) {
#
#	The original idea was to run EXPLAIN and provide the query plan for each test case dumped.
#	However, for crashing queries, running EXPLAIN frequently crashes as well, so we disable it for the time being.
#
#					$test .= "/* Query plan Server $ex:\n";
#					my $plan = $executors->[$ex]->execute("EXPLAIN EXTENDED $query", 1);
#					
#					foreach my $row (@{$plan->data()}) {
#						$test .= "# |".join("|", @$row)."|\n";
#					}
#
#					$test .= "# Extended: \n# ".join("# \n", map { $_->[2] } @{$plan->warnings()})."\n";
#					$test .= "# */\n\n";
				}
			}
		}

		if (
			(defined $results) &&
			(defined $results->[$query_id])
		) {
			$test .= _comment("Diff:",$useHash)."\n\n";

			my $diff = GenTest::Comparator::dumpDiff(
				$simplifier->results()->[$query_id]->[0],
				$simplifier->results()->[$query_id]->[1]
			);

			$test .= _comment($diff,$useHash)."\n\n\n";
		}

		if ($#$participating_tables > -1) {
			foreach my $tab (@$participating_tables) {
				$test .= "DROP TABLE $tab;\n";
			}
		}
	
		$test .= _comment("End of test case for query $query_id",$useHash)."\n\n";
	}

	return $test;
}

sub executors {
	return $_[0]->[SIMPLIFIER_EXECUTORS];
}

sub queries {
	return $_[0]->[SIMPLIFIER_QUERIES];
}

sub results {
	return $_[0]->[SIMPLIFIER_RESULTS];
}

1;
