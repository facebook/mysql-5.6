# Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
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

package GenTest::Validator::ExecutionTimeComparator;

################################################################################
#
# This validator compares the execution times of queries against two different 
# servers. It may repeat each suitable query and compute averages if configured
# to do so (see below).
#
# If the ratio between the execution times of the two servers is above or below
# a given threshold, the query is written to the test's output as well as a
# CSV text file (optional) at the end of the test run.
#
# The validator may be configured by editing this file (adjusting the values
# of certain variables and constants). Configurable properties include:
#   - minimum ratio for which we want to report queries
#   - minumum execution time to care about
#   - maximum execution time to care about
#   - maximum result set size (number of rows)
#   - whether or not to compare EXPLAIN output
#   - whether or not to repeat queries N times and compute average execution 
#     times and/or other statistics to avoid false positives.
#   - whether or not to repeat given queries against multiple tables.
#   - whether or not to write results to a file, and this file's name.
#
# Requires the use of two basedirs.
# Uses the module Statistics::Descriptive if applicable and available.
#
################################################################################

require Exporter;
@ISA = qw(GenTest GenTest::Validator);

use strict;

use GenTest;
use GenTest::Constants;
use GenTest::Comparator;
use GenTest::Result;
use GenTest::Validator;

use Carp;
use Data::Dumper;
use File::Basename;


# Configurable constants:
use constant MIN_DURATION   => 0.02;  # (seconds) Queries with shorter execution times are not processed.
use constant MAX_DURATION   => 300;   # (seconds) Queries with longer execution times are not processed.
use constant MIN_RATIO      => 1.5;   # Minimum speed-up or slow-down required in order to report a query
use constant MAX_ROWS       => 200;   # Skip query if initial execution resulted in more than so many rows.
use constant MIN_SAMPLES    => 0;     # Minimum number of execution time samples to do for each query per server.
use constant MAX_SAMPLES    => 0;     # Max number of execution time samples per query per server. Must be no less than MIN_SAMPLES.
use constant MAX_DEVIATION  => 15;    # Percentage of mean below which we want the standard deviation to be (if statistics is used).

#### NOTES:
#
# 1) A query is repeated if MAX_SAMPLES > 0 and MIN_SAMPLES > 0.
#    MAX_SAMPLES cannot be lower than MIN_SAMPLES.
#    If the Statistics::Descriptive module is not available, the query will be 
#    repeated MAX_SAMPLES times if both are above 0.
#
# 2) If a query is repeated, the original result is discarded.
#    If a query is not repeated, numbers from the original execution will be 
#    used for comparison.
#
# 3) When a query is repeated, it is repeated at least MIN_SAMPLES times.
#    If MIN_SAMPLES = 0, the query will not be repeated (original results will
#    be used).
#    One sample = one measurement of execution time for a given query.
#
# 4) If the Statistics::Descriptive module is available and sampling results 
#    then have a relative standard deviation (percentage of the mean) of less 
#    than MAX_DEVIATION, sampling is ended. Otherwise, sampling continues until 
#    sufficiently low deviation is reached, or until a maximum of MAX_SAMPLES 
#    samples (per query per server) is done (whatever occurs first).
#    Even if MIN_SAMPLES = 1 the query must be repeated at least twice in order 
#    to obtain a standard deviation value other than 0.
#    Queries with too high standard deviation will not be included in the final
#    listing of queries above/below the set ratio threshold.
#
#    If the Statistics::Descriptive module is not available, standard deviations
#    will not be computed and the MAX_DEVIATION setting will be ignored.
####

# Helper variable to determine statistics support. See BEGIN block.
my $have_statistics_mod;

# CSV file with result data. Set variable to undef if you want to disable this.
my $outfile = "rqg_executiontimecomp.txt";

# Name of this file, used in various messages.
my $thisFile;   # see BEGIN block

# Tables against which to re-run the query and repeat validation.
# See BEGIN block for contents and further details.
my @tables;

my $skip_explain = 0;   # set to 0 to do EXPLAIN comparisons, or 1 to skip.


# Result variables, counters
my @execution_times;
my %execution_ratios;
my @unstable_queries;
my $total_queries = 0;      # Number of comparable queries
my $candidate_queries = 0;  # Number of original queries with results entering this validator
my $different_plans = 0;    # Number of queries with different query plan from EXPLAIN
my $zero_queries = 0;       # Number of queries with execution time of zero
my $quick_queries = 0;      # Number of queries with execution times lower than MIN_DURATION for both servers
my $slow_queries = 0;       # Number of queries with execution times higher than MAX_DURATION for either server.
my $over_max_rows = 0;      # Number of queries that produce more than MAX_ROWS rows
my $non_status_ok = 0;      # Number of failed queries (status != STATUS_OK)
my $non_selects = 0;        # Number of non-SELECT statements
my $no_results = 0;         # Number of (rejected) queries with no results (original query/tables only)
my $unstable_queries = 0;   # Number of queries with too much variation of results (relative std_dev higher than MAX_DEVIATION)

# If a query modified by this validator fails, a non-ok status may be returned 
# from the validator even if the original query executed OK.
# Still, if a table variation query does not pass the various threshold checks,
# the total status is not returned until all table variations are done.
# This allows one to specify a small table in the grammar and repeat queries
# against increasingly larger tables.
# Regular repetitions against the same table will stop if the first
# execution did not pass the various checks.
my $total_status = STATUS_OK;


sub BEGIN {
    $thisFile = basename(__FILE__);
    $thisFile =~ s/\.pm//;  # remove .pm suffix from file name
    # Check module requirements
    eval {
        require Statistics::Descriptive;
        $have_statistics_mod = 1;
        say($thisFile.' found Statistics::Descriptive module version '.Statistics::Descriptive->VERSION);
    };
    if (not defined $have_statistics_mod) {
        say('Perl module Statistics::Descriptive not found. '.
            $thisFile.' will not take result stability into consideration.');
    }

    # Check settings
    if (MAX_SAMPLES < MIN_SAMPLES) {
        croak("ERROR: Invalid $thisFile settings! ".
              "MAX_SAMPLES (".MAX_SAMPLES.") must not be less than ".
              "MIN_SAMPLES (".MIN_SAMPLES.")");
    }


    # @tables: Tables against which we should repeat the original query and 
    # re-validate.
    # Example: Configure the grammar to produce queries only against a single 
    #          table. Do systematic testing of different tables by setting e.g.:
    #
    #   @tables = ('AA', 'B', 'BB', 'C', 'CC', 'D', 'DD', 'E');
    @tables = ();

    say($thisFile.' will repeat suitable queries against '.
        scalar(@tables)." tables: @tables") if scalar(@tables) > 0;
    if (MAX_SAMPLES > 0 && MIN_SAMPLES > 0) {
        if (defined $have_statistics_mod) {
            say($thisFile.' will gather at least '.MIN_SAMPLES.
                ' samples and at most '.MAX_SAMPLES.' samples of suitable '.
                'queries and calculate statistics.');
            say($thisFile.' will not include results with standard '.
                'deviation of more than '.MAX_DEVIATION.'% of the mean in '.
                'final test reports.');
        } else {
            say($thisFile.' will perform '.MAX_SAMPLES.' result samples of '.
                'suitable queries and calculate averages if applicable.');
        }
    } else {
        say($thisFile.' will compare original execution times '.
            'only. If a higher level of statistical significance is needed, '.
            "set the number of desired samples in the validator code.");
    }
}


sub validate {
    # For each original query entering this validator, do this...

    my ($comparator, $executors, $results) = @_;

    if ($#$results != 1) {
        $no_results++;
        return STATUS_WONT_HANDLE;
    }

    my $query = $results->[0]->query();
    $candidate_queries++;

    if ($query !~ m{^\s*SELECT}sio) {
        $non_selects++;
        return STATUS_WONT_HANDLE;
    }
    if ($results->[0]->status() != STATUS_OK || $results->[1]->status() != STATUS_OK) {
        $non_status_ok++;
        return STATUS_WONT_HANDLE
    }

    # First check the original query.
    # This will also repeat the query if a sample size is set,
    # unless some conditions (duration, result set size, etc.) are not met.
    my $compare_status = compareDurations($executors, $results, $query);
    $total_status = $compare_status if $compare_status > $total_status;


    # In the case of @tables being defined, we try to repeat each query against
    # the specified tables. We skip this if we cannot recognize the original
    # table name.
    if (defined @tables) {
        my $tableVarStatus = doTableVariation($executors, $results, $query, $total_status);
        $total_status = $tableVarStatus if $tableVarStatus > $total_status;
    }


    return $total_status;
}


sub compareDurations {
    # $results is from the original query execution (outside this validator),
    # or undefined if this is a query that is a variation of the original.
    my ($executors, $results, $query) = @_;

    # In case of table variation we do not have any results for the new 
    # query, so we need to execute it once, for EXPLAIN comparison etc.
    if ($results == undef) {
        $results->[0] = $executors->[0]->execute($query);
        $results->[1] = $executors->[1]->execute($query);
    }

    my $time0 = $results->[0]->duration();
    my $time1 = $results->[1]->duration();

    if ($time0 == 0 || $time1 == 0) {
        $zero_queries++;
        return STATUS_WONT_HANDLE;
    }

    # We check for MAX_DURATION prior to repeating, to avoid wasting time.
    # If sampling is not enabled one might as well continue, but we
    # bail here anyway to have a consistent meaning of MAX_DURATION setting.
    if ($time0 > MAX_DURATION || $time1 > MAX_DURATION) {
        $slow_queries++;
        return STATUS_WONT_HANDLE;
    }

    if ($results->[0]->rows() > MAX_ROWS) {
        $over_max_rows++;
        return STATUS_WONT_HANDLE;
    }

    my @times;  # execution time per server (mean of samples or original)
    my @relative_std_devs; # standard deviation for each sampling, relative to the mean.
                           # Only used if Statistics module is used.

    if (MAX_SAMPLES < 1 || MIN_SAMPLES < 1) {
        # No sampling/repetition is desired. Use only original values.
        @times = ($time0, $time1);
    } else {
        # Sampling/repetition is desired. Discard original values, use new ones.
        if ($have_statistics_mod) {
            my ($times_ref, $relative_std_devs_ref) = sampleWithStatisticsModule($executors, $query);
            # $times_ref may be undefined if sufficient statistical confidence was not reached.
            # In that case we assume the query has been reported if necessary 
            # and discontinue further validation of the query.
            return STATUS_WONT_HANDLE if not defined $times_ref;
            @times = @${times_ref};
            @relative_std_devs = @${relative_std_devs_ref};
        } else {
            # Statistics module is not available. We do a simple calculation of
            # the mean (average) value instead.
            @times = sampleSimple($executors, $query);
        }
    }

    if ($times[0] < MIN_DURATION && $times[1] < MIN_DURATION) {
            $quick_queries++;
            return STATUS_WONT_HANDLE;
    }

    # We only do EXPLAIN checking once.
    if ($skip_explain == 0) {
        my @explains;
        foreach my $executor_id (0..1) {
            my $explain_extended = $executors->[$executor_id]->dbh()->selectall_arrayref("EXPLAIN EXTENDED $query");
            my $explain_warnings = $executors->[$executor_id]->dbh()->selectall_arrayref("SHOW WARNINGS");
            $explains[$executor_id] = Dumper($explain_extended)."\n".Dumper($explain_warnings);
        }

        $different_plans++ if $explains[0] ne $explains[1];
    }


    # We prepare a file for output of per-query performance numbers.
    # Only do this if the file is not already open.
    if (defined $outfile && (tell OUTFILE == -1) ) {
        open (OUTFILE, ">$outfile");
        print(OUTFILE "# Numbers from the RQG's $thisFile validator.\n\n");
        if (defined @relative_std_devs) {
            print(OUTFILE "ratio\treversed_ratio\ttime0\ttime1\trelative_std_dev0 (\%)\trelative_std_dev1 (\%)\tquery\n");
        } else {
            print(OUTFILE "ratio\treversed_ratio\ttime0\ttime1\tquery\n");
        }
    }

    my $ratio = $times[0] / $times[1];
    $ratio = sprintf('%.4f', $ratio) if $ratio > 0.0001;
    my $reversed_ratio = sprintf('%.4f', ($times[1]/$times[0]));

    # Print both queries that became faster and those that became slower
    if ( ($ratio >= MIN_RATIO) || ($ratio <= (1/MIN_RATIO)) ) {

        # First, print to the log...
        my $output = "ratio = $ratio; ".
                     "time0 = ".$times[0]." sec; ".
                     "time1 = ".$times[1]." sec; ";
        if (defined @relative_std_devs) {
            $output .= "rel_std_dev0: ".sprintf('%.2f', $relative_std_devs[0])."; ".
                       "rel_std_dev1: ".sprintf('%.2f', $relative_std_devs[1])."; ";
        }
        $output .= "query: $query";
        say($output);

        # Also print to output file...
        if (defined $outfile) {
            my $file_output = $ratio."\t".$reversed_ratio."\t".
                              $times[0]."\t".$times[1]."\t";
            if (defined @relative_std_devs) {
                $file_output .= sprintf('%.2f', $relative_std_devs[0])."\t".
                                sprintf('%.2f', $relative_std_devs[1])."\t";
            }
            $file_output .= $query."\n";
            print(OUTFILE $file_output);
        }
    }
    #else:
    #  Ratio is too low, don't report the query.

    $total_queries++;
    $execution_times[0]->{sprintf('%.1f', $times[0])}++;
    $execution_times[1]->{sprintf('%.1f', $times[1])}++;

    push @{$execution_ratios{sprintf('%.1f', $ratio)}}, $query;

    return STATUS_OK;

}


# sampleStatistics()
#
# Execute queries and collect statistics needed in order to tell whether one
# server is better than the other.
# 
# Requires Statistics::Desriptive module installed.
#
# Input:  - array of executors (two executors)
#         - query to be validated
#
# Output: - array of execution times
#         - array of relative standard deviations
#
sub sampleWithStatisticsModule() {
    my ($executors, $query) = @_;
    my @exec_times;    # array of execution times (mean values), one per server.

    # Prepare statistical calculation.
    $Statistics::Descriptive::Tolerance = 1e-24; # div by 0 check
    my @stats;  # one statistics object per server

    # Repeat the same query, collect results (depending on settings).
    # If repeating, use one server at a time to allow it to stabilize.
    # Do the same for each server (executor):
    foreach my $server (0..1) {
        # Initialize statistical calculation objects.
        $stats[$server] = Statistics::Descriptive::Full->new();
        my $stat = $stats[$server];
        # Repetition is enabled, so collect results to replace the original.
        # Repeat until we have some idea of statistical significance, 
        # or until a max number of samples
        my $repeats = 0;
        while ($repeats < MAX_SAMPLES) {
            $repeats++;
            my $time = $executors->[$server]->execute($query)->duration();
            $stat->add_data($time);
            my $desired_absolute_deviation = ($stat->mean() * (MAX_DEVIATION/100));
            if($repeats >= MIN_SAMPLES && 
               ($repeats > 1) &&
               ($stat->standard_deviation() <= $desired_absolute_deviation) ) {
                # Standard deviation was good enough, no further samples needed.
                # Note that if MIN_SAMPLES is 1, the standard deviation will be 
                # 0, so we need to do one more sample to get proper numbers,
                # hence the (repeats > 1) part in the 'if' check.
                last;
            }
            if($repeats == MAX_SAMPLES) {
                # No more samples allowed but standard deviation is still not good enough.
                # Report the query and stop validating.
                my $rel_std_dev = ($stat->standard_deviation()/$stat->mean())*100;
                say($thisFile." unable to obtain sufficient stability for query $query");
                say("   Discarded query based on $repeats samples against server$server ".
                    "with sample range ".$stat->sample_range()." and relative standard ".
                    "deviation of ".sprintf('%.2f', $rel_std_dev).'%');
                $unstable_queries++;
                push(@unstable_queries, $query);
                return undef;
            }
        }
    }

    # We have completed the sampling. Now we will prepare reporting of results.
    # Details will be written per query if debug is enabled.

    if (rqg_debug()) {
        say("############# QUERY $candidate_queries #############");
        say("Query: ".$query);
    }

    my @std_devs;          # standard deviation of the samples, per server
    my @relative_std_devs; # std_dev as percentage of the mean of the samples, per server

    foreach my $id (0..1) {
        my $stat = $stats[$id];
        $exec_times[$id] = $stat->mean();
        $std_devs[$id] = $stat->standard_deviation();
        $relative_std_devs[$id] = (($std_devs[$id]/$stat->mean())*100);

        # If RQG debug is enabled, we print some statistics along with each query.
        if (rqg_debug()) {
            say("------------- server $id -------------");
            say("samples      : ".$stat->count() );
            say("mean         : ".$stat->mean() );
            if ($stat->count() > 2) {
                # trimmed mean: We remove the highest and lowest result before calculating the mean.
                say("trimmed mean : ".$stat->trimmed_mean(1/$stat->count(),1/$stat->count()) );
            }
            my $std_err = ($stat->standard_deviation()/sqrt($stat->count()));
            say("median       : ".$stat->median() );
            say("variance     : ".$stat->variance() );
            say("std dev      : ".$stat->standard_deviation() );
            say("std error    : ".$std_err );
            # if count was > 30, we could say that it is 68% chance that the real mean is within mean-stderror and mean+stderror
            say("relat stddev : ".sprintf('%.2f', $relative_std_devs[$id]).'%' );
            say("sample range : ".$stat->sample_range() );
            say("frequency distribution, 4 bins:");
            my %f = $stat->frequency_distribution(4);
            for (sort {$a <=> $b} keys %f) {
                say("  key = $_, count = ".$f{$_}."\n");
            }
        }
    }
    return (\@exec_times, \@relative_std_devs);

}


# sampleSimple()
#
# Execute queries and calculate average (mean) values.
#
# Input:  - array of executors (two executors)
#         - query to be validated
#
# Output: - array of execution times
#
sub sampleSimple() {
    my ($executors, $query) = @_;
    my @exec_times;    # array of execution times (mean values), one per server.

    # If repetitions is desired, we ignore the results from the original query 
    # execution (from before entering the validator), as it may be off for some 
    # reason. 
    #
    # Repeat the same query, collect results (depending on settings).
    my @sum_times;
    foreach my $id (0..1) {
        my $repeats_left = MAX_SAMPLES;
        while ($repeats_left > 0) {
            $sum_times[$id] += $executors->[$id]->execute($query)->duration();
            $repeats_left--;
        }
    }
    # Calculate the mean.
    # We use only the first 4 decimals of the result, but include all decimals 
    # in calculations.
    if (MAX_SAMPLES > 0) {
        foreach my $id (0..1) {
            push(@exec_times, sprintf('%.4f', ($sum_times[$id] / MAX_SAMPLES)) );
        }
    }
    return @exec_times;
}


sub doTableVariation() {
    my ($executors, $results, $query, $status) = @_;
    
    # The goal is to repeat the same query against different tables.
    # We assume the query is simple, with a single "FROM <letter(s)>" construct.
    # Otherwise, skip the repeating (this detection may be improved).

    my $lookfor_pattern = "FROM ([a-zA-Z]+) ";

    # Find the original table name to avoid repeating it unnecessarily.
    my $orig_table;
    if ($query =~ m{$lookfor_pattern}) {
        $orig_table = $1;
    } else {
        # Query structure not recognized, unable to find original table name.
        # Skip further processing of this query.
        return $status;
    }

    # Construct new queries based on the original, replacing the table name.
    foreach my $table ( @tables ) {
        if ($table eq $orig_table) {
            # do not repeat the original
            next;
        }
        my $new_query = $query;
        $new_query =~ s/$lookfor_pattern/FROM $table /; # change table name

        # We pass undef as "results" parameter so that the new query will be
        # executed at least once.
        my $compare_status = compareDurations($executors, undef, $new_query);

        # One table variation query may yield STATUS_WONT_HANDLE, while others
        # may yield STATUS_OK. We return the status from the last variation and
        # ignore the others, assuming previous checks have taken care of 
        # unwanted queries and errors.

        $status = $compare_status;
    }

    return $status;
}


sub DESTROY {
    say("Total number of queries entering $thisFile: $candidate_queries");
    say("Extra tables used for query repetition: ".scalar(@tables));
    say("Minimum samples of execution time per query: ".MIN_SAMPLES);
    say("Maximum samples of execution time per query: ".MAX_SAMPLES);
    say("Maximum relative standard deviation: ".MAX_DEVIATION.'% of mean value') 
        if $have_statistics_mod and MIN_SAMPLES > 0;
    say("Skipped queries:");
    say("  Excluded non-SELECT queries: ".$non_selects);
    say("  Queries with execution time of 0: ".$zero_queries);
    say("  Queries shorter than MIN_DURATION (".MIN_DURATION." s): ".$quick_queries);
    say("  Queries longer than MAX_DURATION (".MAX_DURATION." s): ".$slow_queries);
    say("  Queries returning more than MAX_ROWS (".MAX_ROWS.") rows: ".$over_max_rows);
    say("  Queries with missing results: ".$no_results);
    say("  Queries not returning STATUS_OK: ".$non_status_ok);
    say("  Queries with too unstable durations (relative std_dev higher than ".
        MAX_DEVIATION.'% of mean): '.$unstable_queries) if defined $have_statistics_mod  and MIN_SAMPLES > 0;
    say("Queries suitable for execution time comparison: $total_queries");
    say("Queries with different EXPLAIN plans: $different_plans") if ($skip_explain == 0);
    say("Notable execution times for basedir0 and basedir1, respectively:"); 
    print Dumper \@execution_times;
    foreach my $ratio (sort keys %execution_ratios) {
        print "ratio = $ratio; queries = ".scalar(@{$execution_ratios{$ratio}}).":\n";
        if (
            ($ratio <= (1 - (1 / MIN_RATIO) ) ) ||
            ($ratio >= MIN_RATIO)
        ) {
            foreach my $query (@{$execution_ratios{$ratio}}) {
                print "$query\n";
            }
        }
    }
    if (scalar(@unstable_queries) > 0) {
        say("Unstable queries:");
        print Dumper \@unstable_queries;
    }
    if (defined $outfile && -e $outfile) {
        close(OUTFILE);
        say("See file $outfile for results from $thisFile");
    }
}

1;
