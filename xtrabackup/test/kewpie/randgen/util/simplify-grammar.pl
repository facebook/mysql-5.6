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
use DBI;
use Carp;
use Getopt::Long;
use Data::Dumper;

use GenTest;
use GenTest::Constants;
use GenTest::Grammar;
use GenTest::Properties;
use GenTest::Simplifier::Grammar;
use Time::HiRes;

#
# RQG grammar simplification with an oracle() function based on
# 1. RQG exit status codes (-> desired_status_codes)
# 2. expected RQG protocol output (-> expected_output)
# Hint: 2. will be not checked if 1. already failed
#
# You need to adjust parameters to your use case and environment.
# 1. Copy simplify-grammar_template.cfg to for example 1.cfg
# 2. Adjust the settings
# 2. perl util/simplify-grammar.pl --config 1.cfg
#
# This script is used to simplify grammar files to the smallest form
# that will still reproduce the desired outcome
# For the purpose, the GenTest::Simplifier::Grammar module provides
# progressively simple grammars, and we define an oracle() function
# that runs those grammars with the RQG and reports if the RQG returns
# the desired status code (usually something like
# STATUS_SERVER_CRASHED
#
# For more information, please see:
#
# http://forge.mysql.com/wiki/RandomQueryGeneratorSimplification
#

# get configuration

my $options = {};
GetOptions($options, 'config=s', 'trials=i','storage_prefix=s','expected_output=s@');
my $config = GenTest::Properties->new(
    options => $options,
    legal => ['desired_status_codes',
              'expected_output',
              'initial_grammar_file',
              'mask',
              'mask-level',
              'grammar_flags',
              'trials',
              'initial_seed',
              'search_var_size',
              'rqg_options',
              'vardir_prefix',
              'storage_prefix'],
    required=>['rqg_options',
               'initial_grammar_file',
               'vardir_prefix',
               'storage_prefix'],
    defaults => {expected_output => [],
                 desired_status_codes => [+STATUS_ANY_ERROR]}
    );

# Dump settings
say("SIMPLIFY RQG GRAMMAR BASED ON EXPECTED CONTENT WITHIN SOME FILE");
say("---------------------------------------------------------------");
$config->printProps;
say("---------------------------------------------------------------");

## Calculate mysqld and rqg options

my $mysqlopt = $config->genOpt('--mysqld=--', $config->rqg_options->{mysqld});

## The one below is a hack.... Need support for nested options like these
delete $config->rqg_options->{mysqld};

my $rqgoptions = $config->genOpt('--', 'rqg_options');

# Determine some runtime parameter, check parameters, ....

my $run_id = time();

say("The ID of this run is $run_id.");

my $initial_grammar;

if ($config->property('mask') > 0) {
   my $initial_grammar_obj = GenTest::Grammar->new( 'grammar_file'  => $config->initial_grammar_file );
   my $top_grammar = $initial_grammar_obj->topGrammar($config->property('mask-level'), "query", "query_init");
   my $masked_top = $top_grammar->mask($config->property('mask'));
   $initial_grammar = $initial_grammar_obj->patch($masked_top);
} else {
   open(INITIAL_GRAMMAR, $config->initial_grammar_file) or croak "Unable to open initial_grammar_file '" . $config->initial_grammar_file . "' : $!";
   read(INITIAL_GRAMMAR, $initial_grammar , -s $config->initial_grammar_file);
   close(INITIAL_GRAMMAR);
}

if ( ! -d $config->vardir_prefix ) {
   croak("vardir_prefix '" . $config->vardir_prefix . "' is not an existing directory");
}

# Calculate a unique vardir (use $MTR_BUILD_THREAD or $run_id)

my $vardir = $config->vardir_prefix . '/var_' . $run_id;
mkdir ($vardir);
push my @mtr_options, "--vardir=$vardir";

if ( ! -d $config->storage_prefix) {
   croak("storage_prefix '" . $config->storage_prefix . "' is not an existing directory");
}
my $storage = $config->storage_prefix.'/'.$run_id;
say "Storage is $storage";
mkdir ($storage);

my $errfile = $vardir . '/log/master.err';

my $iteration;
my $good_seed = $config->initial_seed;

my $simplifier = GenTest::Simplifier::Grammar->new(
    grammar_flags => $config->grammar_flags,
    oracle => sub {
        $iteration++;
        my $oracle_grammar = shift;

        my $current_grammar = $storage . '/' . $iteration . '.yy';
        open (GRAMMAR, ">$current_grammar")
           or croak "unable to create $current_grammar : $!";
        print GRAMMAR $oracle_grammar;
        close (GRAMMAR);


        foreach my $trial (1..$config->trials) {
            say("run_id = $run_id; iteration = $iteration; trial = $trial");

            # $current_seed -- The seed value to be used for the next run.
            # The test results of many grammars are quite sensitive to the
            # seed value.
            # 1. Run the first trial on the initial grammar with
            #    $config->initial_seed .  This should raise the chance
            #    that the initial oracle check passes.
            # 2. Run the first trial on a just simplified grammar with the
            #    last successfull seed value. In case the last
            #    simplification did remove some random determined we
            #    should have a bigger likelihood to reach the expected
            #    result.
            # 3. In case of "threads = 1" it turned out that after a minor
            #    simplification the desired bad effect disappeared
            #    sometimes on the next run with the same seed value
            #    whereas a different seed value was again
            #    successful. Therefore we manipulate the seed value.  In
            #    case of "threads > 1" this manipulation might be not
            #    required, but it will not make the conditions worse.
            my $current_seed = $good_seed - 1 + $trial;

            my $current_rqg_log = $storage . '/' . $iteration . '-'. $trial . '.log';

            my $start_time = Time::HiRes::time();

            # Note(mleich): In case of "threads = 1" it turned out that
            #    after a minor simplification the desired bad effect
            #    disappeared sometimes on the next run with the same
            #    seed value whereas a different seed value was again
            #    successful. Therefore we manipulate the seed value.  In
            #    case of "threads > 1" this manipulation might be not
            #    required, but it will not make the conditions worse.

            my $rqgcmd =
                "perl runall.pl $rqgoptions $mysqlopt ".
                "--grammar=$current_grammar ".
                "--vardir=$vardir ".
                "--seed=$current_seed >$current_rqg_log 2>&1";

            say($rqgcmd);
            my $rqg_status = system($rqgcmd);
            $rqg_status = $rqg_status >> 8;

            my $end_time = Time::HiRes::time();
            my $duration = $end_time - $start_time;

            say("rqg_status = $rqg_status; duration = $duration");

            return ORACLE_ISSUE_NO_LONGER_REPEATABLE
                if $rqg_status == STATUS_ENVIRONMENT_FAILURE;

            foreach my $desired_status_code (@{$config->desired_status_codes}) {
                if (($rqg_status == $desired_status_code) ||
                    (($rqg_status != 0) && ($desired_status_code == STATUS_ANY_ERROR))) {
                    # "backtrace" output (independend of server crash
                    # or RQG kills the server) is in $current_rqg_log
                    open (my $my_logfile,'<'.$current_rqg_log)
                        or croak "unable to open $current_rqg_log : $!";
                    # If open (above) did not fail than size
                    # determination must be successful.
                    my @filestats = stat($current_rqg_log);
                    my $filesize = $filestats[7];
                    my $offset = $filesize - $config->search_var_size;
                    # Of course read fails if $offset < 0
                    $offset = 0 if $offset < 0;
                    read($my_logfile, my $rqgtest_output, $config->search_var_size, $offset );
                    close ($my_logfile);
                    # Debug print("$rqgtest_output");

                    # Every element of @expected_output must be found
                    # in $rqgtest_output.
                    my $success = 1;
                    foreach my $expected_output (@{$config->expected_output}) {
                        if ($rqgtest_output =~ m{$expected_output}sio) {
                            say ("###### Found pattern:  $expected_output ######");
                        } else {
                            say ("###### Not found pattern:  $expected_output ######");
                            $success = 0;
                            last;
                        }
                    }
                    if ( $success ) {
                        say ("###### SUCCESS with $current_grammar ######");
                        $good_seed = $current_seed;
                        return ORACLE_ISSUE_STILL_REPEATABLE;
                    }
                } # End of check if the output matches given string patterns
            } # End of loop over desired_status_codes
            if ($rqg_status == STATUS_OK) {
               # Run with exit status 0 -> RQG output is not of interest
               unlink($current_rqg_log);
            }
        } # End of loop over the trials
        return ORACLE_ISSUE_NO_LONGER_REPEATABLE;
    }
    );

my $simplified_grammar = $simplifier->simplify($initial_grammar);

print "Simplified grammar:\n\n$simplified_grammar;\n\n" if defined $simplified_grammar;
