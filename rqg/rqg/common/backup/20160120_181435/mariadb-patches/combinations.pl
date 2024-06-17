#!/usr/bin/perl

# Copyright (c) 2008, 2011 Oracle and/or its affiliates. All rights reserved.
# Copyright (c) 2013, Monty Program Ab.
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
use lib "$ENV{RQG_HOME}/lib";
use Carp;
#use List::Util 'shuffle';
use Cwd;
use GenTest;
use GenTest::Random;
use GenTest::Constants;
use Getopt::Long;
use GenTest::BzrInfo; 
use Data::Dumper;
use File::Basename;

if (defined $ENV{RQG_HOME}) {
    if (osWindows()) {
        $ENV{RQG_HOME} = $ENV{RQG_HOME}.'\\';
    } else {
        $ENV{RQG_HOME} = $ENV{RQG_HOME}.'/';
    }
} else {
    $ENV{RQG_HOME} = dirname(Cwd::abs_path($0));
}

if ( osWindows() )
{
    require Win32::API;
    my $errfunc = Win32::API->new('kernel32', 'SetErrorMode', 'I', 'I');
    my $initial_mode = $errfunc->Call(2);
    $errfunc->Call($initial_mode | 2);
};

my $logger;
eval
{
    require Log::Log4perl;
    Log::Log4perl->import();
    $logger = Log::Log4perl->get_logger('randgen.gentest');
};

$| = 1;
my $ctrl_c = 0;
    
$SIG{INT} = sub { $ctrl_c = 1 };
$SIG{TERM} = sub { exit(0) };
$SIG{CHLD} = "IGNORE" if osWindows();

my ($config_file, $basedir, $vardir, $trials, $duration, $grammar, $gendata, 
    $seed, $testname, $xml_output, $report_xml_tt, $report_xml_tt_type,
    $report_xml_tt_dest, $force, $no_mask, $exhaustive, $start_combination, $debug, $noLog, 
    $threads, $new, $servers, $noshuffle, $clean, $workdir);

my @basedirs=('','');
my $combinations;
my %results;
my @commands;
my $max_result = 0;
my $thread_id = 0;
my $epochcreadir;
my $mtrbt = defined $ENV{MTR_BUILD_THREAD}?$ENV{MTR_BUILD_THREAD}:300;

my $opt_result = GetOptions(
	'config=s' => \$config_file,
	'basedir=s' => \$basedirs[0],
	'basedir1=s' => \$basedirs[0],
	'basedir2=s' => \$basedirs[1],
	'workdir=s' => \$workdir,
	'vardir=s' => \$workdir,
	'trials=i' => \$trials,
	'duration=i' => \$duration,
	'seed=s' => \$seed,
	'force' => \$force,
	'no-mask' => \$no_mask,
	'grammar=s' => \$grammar,
	'gendata=s' => \$gendata,
	'testname=s' => \$testname,
	'xml-output=s' => \$xml_output,
	'report-xml-tt' => \$report_xml_tt,
	'report-xml-tt-type=s' => \$report_xml_tt_type,
	'report-xml-tt-dest=s' => \$report_xml_tt_dest,
    'run-all-combinations-once' => \$exhaustive,
    'start-combination=i' => \$start_combination,
    'debug' => \$debug,
    'no-log' => \$noLog,
    'parallel=i' => \$threads,
    'new' => \$new,
    'servers=i' => \$servers,
    'no-shuffle' => \$noshuffle,
    'clean' => \$clean
);

my $prng = GenTest::Random->new(
	seed => $seed eq 'time' ? time() : $seed
);

open(CONF, $config_file) or croak "unable to open config file '$config_file': $!";
read(CONF, my $config_text, -s $config_file);
eval ($config_text);
croak "Unable to load $config_file: $@" if $@;

if (!defined $servers) {
    $servers = 1;
    $servers = 2 if $basedirs[1] ne '';
}

croak "--servers may only be 1 or 2" if !($servers == 1 or $servers == 2);

my $logToStd = !osWindows() && !$noLog;

my $bzrinfo = GenTest::BzrInfo->new(
    dir => cwd()
    ); 
my $revno = $bzrinfo->bzrRevno();
my $revid = $bzrinfo->bzrRevisionId();

if ((defined $revno) && (defined $revid)) {
    say(cwd()." Revno: $revno");
    say(cwd()." Revision-Id: $revid");
} else {
    say(cwd().' does not look like a bzr branch, cannot get revision info.');
} 

if (not defined $threads) {
    $threads=1;
} else {
    if ((not defined $trials) and (not defined $exhaustive)) {
        croak("When using --parallel, also add either or both of these options: --run-all-combinations-once (exhaustive run) and/or --trials=x (random run).\n(Both options combined gives a non-random exhaustive run, yet limited by the number of trials.)");
    }
    $logToStd = 0;
}

say("Using workdir=".$workdir);

my $comb_count = $#$combinations + 1;

my $total = 1;
my $thread_id;
if ($exhaustive) {
    foreach my $comb_id (0..($comb_count-1)) {
        $total *= $#{$combinations->[$comb_id]}+1;
    }
    if (defined $trials) {
        if ($trials < $total) {
            say("You specified --run-all-combinations-once, which gives $total combinations, but then limited the same with --trials=$trials");
        } else {
            $trials = $total;
        }
    } else {
        $trials = $total;
    }
}

my %pids;
for my $i (1..$threads) {
    my $pid = fork();
    if ($pid == 0) {
        ## Child
        $thread_id = $i;
        mkdir($workdir);
        
        if ($exhaustive) {
            doExhaustive(0);
        } else {
            doRandom();
        }
        ## Children does not continue this loop
        last;
    } else {
        ##Parent
        $thread_id = 0;
        $pids{$pid}=$i;
        say("Started thread [$i] pid=$pid");
    }
}

if ($thread_id > 0) {
    ## Child
    ##say("[$thread_id] Summary of various interesting strings from the logs:");
    ##say("[$thread_id] ". Dumper \%results);
    #foreach my $string ('text=', 'bugcheck', 'Error: assertion', 'mysqld got signal', 'Received signal', 'exception') {
    #    system("grep -i '$string' $workdir/trial*log");
    #} 
    
    say("[$thread_id] will exit with exit status ".status2text($max_result).
        "($max_result)");
    exit($max_result);
} else {
    ## Parent
    my $total_status = 0;
    while(1) {
        my $child = wait();
        last if $child == -1;
        my $exit_status = $? > 0 ? ($? >> 8) : 0;
        #say("Thread $pids{$child} (pid=$child) exited with $exit_status");
        $total_status = $exit_status if $exit_status > $total_status;
    }
    say("$0 will exit with exit status ".status2text($total_status).
        "($total_status)");
    exit($total_status);
}



## ----------------------------------------------------

my $trial_counter = 0;

sub doExhaustive {
    my ($level,@idx) = @_;
    if ($level < $comb_count) {
        my @alts;
        foreach my $i (0..$#{$combinations->[$level]}) {
            push @alts, $i;
        }
        $prng->shuffleArray(\@alts) if !$noshuffle;
        
        foreach my $alt (@alts) {
            push @idx, $alt;
            doExhaustive($level+1,@idx) if $trial_counter < $trials;
            pop @idx;
        }
    } else {
        $trial_counter++;
        my @comb;
        foreach my $i (0 .. $#idx) {
            push @comb, $combinations->[$i]->[$idx[$i]];
        }
        my $comb_str = join(' ', @comb);
        next if $trial_counter < $start_combination;
        doCombination($trial_counter,$comb_str,"combination");
    }
}

## ----------------------------------------------------

sub doRandom {
    foreach my $trial_id (1..$trials) {
        my @comb;
        foreach my $comb_id (0..($comb_count-1)) {
            my $n = $prng->uint16(0, $#{$combinations->[$comb_id]});
            $comb[$comb_id] = $combinations->[$comb_id]->[$n];
        }
        my $comb_str = join(' ', @comb);        
        doCombination($trial_id,$comb_str,"random trial");
    }
}

## ----------------------------------------------------
sub doCombination {
    my ($trial_id,$comb_str,$comment) = @_;

    return if (($trial_id -1) % $threads +1) != $thread_id;
    say("[$thread_id] Running $comment ".$trial_id."/".$trials);
	my $mask = $prng->uint16(0, 65535);

    my $runall = $new?"runall-new.pl":"runall.pl";

	my $command = "
		perl ".($Carp::Verbose?"-MCarp=verbose ":"").
        (defined $ENV{RQG_HOME} ? $ENV{RQG_HOME}."/" : "" ).
        "$runall $comb_str ";

	$command .= " --queries=100000000" if $comb_str !~ /--queries=/;
	$command .= " --mask=$mask" if $comb_str !~ /-mask/;
	$command .= " --mtr-build-thread=".($mtrbt+($thread_id-1)*2);
	$command .= " --duration=$duration" if $duration ne '';
    foreach my $s (1..$servers) {
        $command .= " --basedir".$s."=".$basedirs[$s-1]." " if $basedirs[$s-1] ne '';
    }
	$command .= " --gendata=$gendata " if $gendata ne '';
	$command .= " --grammar=$grammar " if $grammar ne '';
	$command .= " --seed=$seed " if $seed ne '';
	$command .= " --testname=$testname " if $testname ne '';
	$command .= " --xml-output=$xml_output " if $xml_output ne '';
	$command .= " --report-xml-tt" if defined $report_xml_tt;
	$command .= " --report-xml-tt-type=$report_xml_tt_type " if $report_xml_tt_type ne '';
	$command .= " --report-xml-tt-dest=$report_xml_tt_dest " if $report_xml_tt_dest ne '';

    foreach my $s (1..$servers) {
        $command .= " --vardir".$s."=$workdir/current".$s."_$thread_id " if $command !~ m{--mem}sio && $workdir ne '';
    }
	$command =~ s{[\t\r\n]}{ }sgio;
    if ($logToStd) {
        $command .= " 2>&1 | tee $workdir/trial".$trial_id.'.log';
    } else {
        $command .= " > $workdir/trial".$trial_id.'.log'. " 2>&1";
    }

	$commands[$trial_id] = $command;

	$command =~ s{"}{\\"}sgio;

	# '_epoch' time directory creator extension (only activated if '_epoch' is used anywhere in the command line)
	if ($command =~ m/_epoch/) {
		my $epoch=`date -u '+%s%N' | tr -d '\n'`;
		my $epochdir = defined $ENV{EPOCH_DIR}?$ENV{EPOCH_DIR}:'/tmp';
		$epochcreadir=$epochdir.'/'.$epoch;
		mkdir $epochcreadir or croak "unable to create directory '$epochcreadir': $!";
		say ("[$thread_id] '_epoch' detected in command line. Created directory: $epochcreadir and substituted '_epoch' to it.");
		$command =~ s/_epoch/$epochcreadir/sgo;	
	}

	unless (osWindows())
	{
		$command = 'bash -c "set -o pipefail; '.$command.'"';
	}

    if ($logToStd) {
        say("[$thread_id] $command");
    }
    my $result = 0;
    $result = system($command) if not $debug;

    $result = $result >> 8;
    my $tl = $workdir.'/trial'.$trial_id.'.log';
    if (defined $clean && $result == 0) {
        say("[$thread_id] $runall exited with exit status ".status2text($result)."($result). Clean mode active: deleting this OK log");
        system("rm -f $tl");
    } else {
        say("[$thread_id] $runall exited with exit status ".status2text($result)."($result), see $tl");
    }
    exit($result) if (($result == STATUS_ENVIRONMENT_FAILURE) || ($result == 255)) && (not defined $force);

    if ($result > 0) {
        foreach my $s (1..$servers) {
            $max_result = $result if $result > $max_result;
            my $from = $workdir.'/current'.$s.'_'.$thread_id;
            my $to = $workdir.'/vardir'.$s.'_'.$trial_id;
            say("[$thread_id] Copying $from to $to") if $logToStd;
            if (osWindows()) {
                system("xcopy \"$from\" \"$to\" /E /I /Q");
                system("xcopy \"$from"."_slave\" \"$to\" /E /I /Q") if -e $from.'_slave';
                open(OUT, ">$to/command");
                print OUT $command;
                close(OUT);
            } elsif ($command =~ m{--mem}) {
                system("cp -r /dev/shm/var $to");
                open(OUT, ">$to/command");
                print OUT $command;
                close(OUT);
            } else {
                system("cp -r $from $to");
                system("cp -r $from"."_slave $to") if -e $from.'_slave';
		if ($command =~ m/_epoch/) {
			system("mv $epochcreadir $to");
		}
                open(OUT, ">$to/command");
                print OUT $command;
                close(OUT);
                if (defined $clean) {
                    say("[$thread_id] Clean mode active & failed run (".status2text($result)."): Archiving this vardir");
                    system('rm -f '.$workdir.'/vardir'.$s.'_'.$trial_id.'/tmp/master.sock'); 
                    system('tar zhcf '.$workdir.'/vardir'.$s.'_'.$trial_id.'.tar.gz -C '.$workdir.' ./vardir'.$s.'_'.$trial_id);
		    system("rm -Rf $epochcreadir");
                    system("rm -Rf $to");
                }
            }
        }
    }
    $results{$result >> 8}++;
}
