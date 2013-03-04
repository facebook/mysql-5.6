# Copyright (C) 2008-2010 Sun Microsystems, Inc. All rights reserved.
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

## Hunting for bugs.
##
## Repeated runs of runall with variation of seed and mask values.
##
## There will be a message in case some conditions are fulfilled.
## ==============================================================
## Message about           | Fulfilled conditions
##                         |
## --------------------------------------------------------------------------------------------------
## runall status code      | desired_status_codes is defined and we just reached one of them
## --------------------------------------------------------------------------------------------------
## matching output pattern | (expected_outputs is defined and one of the patterns matched)
##                         | and (desired_status_codes is not defined
##                         |      or desired_status_codes is defined and we just reached one of them)
##
##
## The protocol of some RQG run will be not deleted in case of
## ===========================================================
## runall status code <> 0
## or
## desired_status_codes is not defined, expected_outputs is defined and we just reached one of them
##
## If "--stop_on_match" is set a bughunt terminates in case of
## ===========================================================
## if desired_status_codes is defined and we just reached one of them
## and
## if expected_outputs is defined we found all patterns.
##
## The various options are explained in bughunt_template.cfg.
##

use strict;
use lib 'lib';
use lib '../lib';
use DBI;
use Carp;
use Getopt::Long;
use Data::Dumper;
use File::Copy;

use GenTest;
use GenTest::Constants;
use GenTest::Grammar;
use GenTest::Properties;
use GenTest::Simplifier::Grammar;
use Time::HiRes;

my $options = {};
GetOptions($options,
           'config=s',
           'grammar=s',
           'redefine=s',
           'trials=i',
           'initial_seed=i',
           'storage_prefix=s',
           'expected_outputs=s@',
           'desired_status_codes=s@',
           'rqg_options=s%',
           'basedir=s',
           'vardir_prefix=s',
           'mysqld=s%',
           'stop_on_match!');
my $config = GenTest::Properties->new(
    options => $options,
    legal => ['desired_status_codes',
              'expected_outputs',
              'grammar',
              'gendata',
              'redefine',
              'trials',
              'initial_seed',
              'mask_level',
              'initial_mask',
              'search_var_size',
              'rqg_options',
              'basedir',
              'vardir_prefix',
              'storage_prefix',
              'stop_on_match',
              'mysqld'],
    required=>['rqg_options',
               'grammar',
               'basedir',
               'vardir_prefix',
               'storage_prefix'],
    defaults => {search_var_size=>10000,
                 trials=>1}
    );

# Dump settings
$config->printProps;

## Calculate mysqld and rqg options

my $mysqlopt = $config->genOpt("--mysqld=--",$config->mysqld)
    if defined $config->mysqld;

my $rqgoptions = $config->genOpt('--', 'rqg_options');

# Determine some runtime parameter, check parameters, ....

my $run_id = time();

say("The ID of this run is $run_id.");

if ( ! -d $config->vardir_prefix ) {
    croak("vardir_prefix '"
          . $config->vardir_prefix .
          "' is not an existing directory");
}

my $vardir = $config->vardir_prefix . '/var_' . $run_id;
mkdir ($vardir);
push my @mtr_options, "--vardir=$vardir";

if ( ! -d $config->storage_prefix) {
    croak("storage_prefix '"
          . $config->storage_prefix .
          "' is not an existing directory");
}
my $storage = $config->storage_prefix.'/'.$run_id;
say "Storage is $storage";
mkdir ($storage);

# We must work on a copy of our test grammars because the original grammars could
# be modified during the bughunt or before inspection of the bughunt results.
my $sql_grammar = $storage."/bughunt_sql.yy";
copy($config->grammar, $sql_grammar)
    or croak("File " . $config->grammar . " cannot be copied to " . $sql_grammar );
my $data_grammar = "";
if ( defined $config->gendata ) {
   $data_grammar = $storage."/bughunt_data.zz";
   copy($config->gendata, $data_grammar)
       or croak("File " . $config->gendata . " cannot be copied to " . $data_grammar );
   $rqgoptions = $rqgoptions . " --gendata=" . $data_grammar;
}
my $redefine_grammar = "";
if ( defined $config->redefine ) {
   $redefine_grammar = $storage."/bughunt_sql_redefine.yy";
   copy($config->redefine, $redefine_grammar)
       or croak("File " . $config->redefine . " cannot be copied to " . $redefine_grammar );
   $rqgoptions = $rqgoptions . " --redefine=" . $redefine_grammar;
}

# Some bugs can be investigating using the core file and the server binary.
# Make a copy of the server binary.
# Attention: In the moment we do not use the copy during testing like the grammars.
# FIXME: There is at least one more location where the "mysqld" binary might be stored.
copy($config->basedir.'/sql/mysqld', $storage.'/mysqld');
$rqgoptions = $rqgoptions . " --basedir=" . $config->basedir;

my $good_seed = $config->initial_seed;
my $mask_level = $config->mask_level;
my $good_mask = $config->initial_mask;
my $current_seed;
my $current_mask;
my $current_rqg_log;
my $errfile = $vardir . '/log/master.err';
my $preserve_log;
foreach my $trial (1..$config->trials) {
    say("###### run_id = $run_id; trial = $trial ######");

    $current_seed = $good_seed - 1 + $trial;
    $current_mask = $good_mask - 1 + $trial;
    $current_rqg_log = $storage . '/' . $trial . '.log';
    $preserve_log = 0;

    my $start_time = Time::HiRes::time();

    my $runall =
        "perl runall.pl ".
        " $rqgoptions $mysqlopt ".
        "--grammar=".$sql_grammar." ".
        "--vardir=$vardir ".
        "--mask-level=$mask_level ".
        "--mask=$current_mask ".
        "--seed=$current_seed >$current_rqg_log 2>&1";

    say($runall);
    my $runall_status = system($runall);
    $runall_status = $runall_status >> 8;

    if ($runall_status == STATUS_UNKNOWN_ERROR) {
       say("runall_status = $runall_status");
       say("Maybe the server startup options are wrong.");
       say("Abort");
       exit STATUS_UNKNOWN_ERROR;
    }


    my $end_time = Time::HiRes::time();
    my $duration = $end_time - $start_time;

    say("runall_status = $runall_status; duration = $duration");

    if ($runall_status != 0) {$preserve_log = 1};
    if (defined $config->desired_status_codes) {
        foreach my $desired_status_code (@{$config->desired_status_codes}) {
            if (($runall_status == $desired_status_code) ||
                (($runall_status != 0) &&
                 ($desired_status_code == STATUS_ANY_ERROR))) {
                say ("###### Found status $runall_status ######");
                if (defined $config->expected_outputs) {
                    checkLogForPattern();
                } else {
                    exit STATUS_OK if $config->stop_on_match;
                }
            }
        }
    } elsif (defined $config->expected_outputs) {
        checkLogForPattern();
    }
    # Save storage space by deleting the log of a non important run.
    if ($preserve_log == 0) {
       unlink($current_rqg_log)
    } else {
      # Backup vardir, the grammars and the mysqld binary 
      # Attention: There might be warnings about missing grammar files
      # Sleep a bit with the intention to avoid that the archiver comes up with a
      # warning like "<vardir>/master-data/ib_logfile1: File changed during reading
      sleep 10;
      my $save_cmd = 'tar czf ' . $storage . '/' . $trial . '.tgz ' . $vardir . ' ' . $current_rqg_log . ' ' . $sql_grammar . ' ' . $data_grammar . ' ' . $redefine_grammar . ' ' . $storage.'/mysqld' ;
      say("save_cmd: ->$save_cmd<-");
      my $save_vardir = system($save_cmd);
      # FIXME: Abort in case the call to tar fails
    }

}

#############################

sub checkLogForPattern {
    open (my $my_logfile,'<'.$current_rqg_log)
        or croak "unable to open $current_rqg_log : $!";

    my @filestats = stat($current_rqg_log);
    my $filesize = $filestats[7];
    my $offset = $filesize - $config->search_var_size;

    ## Ensure the offset is not negative
    $offset = 0 if $offset < 0;
    read($my_logfile, my $rqgtest_output,
         $config->search_var_size,
         $offset );
    close ($my_logfile);

    my $match_on_all=1;
    foreach my $expected_output (@{$config->expected_outputs}) {
        if ($rqgtest_output =~ m{$expected_output}sio) {
            say ("###### Found pattern: $expected_output ######");
            $preserve_log = 1;
        } else {
            say ("###### Not found pattern: $expected_output ######");
            $match_on_all = 0;
        }
    }
    exit STATUS_OK if $config->stop_on_match and $match_on_all;
}
