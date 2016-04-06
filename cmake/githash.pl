#!/usr/bin/perl -w

#
# Copyright (c) 2016 Facebook, Inc.
# All rights reserved
#

#
# Git hash/date header file generator
#

use strict;

use Cwd;
use File::Copy qw(copy);

# Simple command line processing that expects just two items
#  1) a subdirectory
#  2) a file name
sub process_cmd_line {
  my $root = "";
  my $infile = "";

  # Loop through the command line arguments
  foreach (@_) {
    if (!length($root)) {
      # If we don't already have the root directory, get it
      $root = $_;
    }
    elsif (!length($infile)) {
      # If we don't already have the file name, get it
      $infile = $_;
    }
    else {
      die "Too many parameters - $_";
    }
  }

  # Check to make sure we got what we expected
  die "Too few parameters, expect githash.pl <git_root> <file>"
      unless length($infile);

  # Return the parameters to the caller
  return ($root, $infile);
}

# Function to retrieve the git hash and date from a repository
sub git_hash_and_date {
  my $subdir = shift;
  my $orgdir = Cwd::getcwd;

  # Switch directories to the specified one
  chdir($subdir) or die "Can't change directory to $subdir";

  # Get the hash and date from the most recent revision in the repository
  my $git_cmd = "git log -1 --format=\"%H;%cd\"";
  open (my $log, "$git_cmd |") or die "Can't run $git_cmd";

  my $githash = "";
  my $gitdate = "";

  # Loop through all the lines - we should only have one line
  while (<$log>) {
    # Search for a line that has a hash, a semicolon and the date
    if (/^([0-9a-f]{7,40});(.*)\n/) {
      die "Unexpected multiple matching log lines" unless !length($githash);
      $githash = $1;
      $gitdate = $2;
    }
  }

  # Make sure we got something
  die "No matching log lines" unless length($githash);

  # Close the input and switch back to the original subdirectory
  close($log);
  chdir($orgdir);

  # Return the found hash and date
  return ($githash, $gitdate);
}

# main function
sub main {
  # expect two parameters -
  #   1) the root of the git repository
  #   2) the file to update with the git hash and date
  (my $root, my $infile) = process_cmd_line(@_);
  my $rocksdb = "$root/rocksdb";
  my $outfile = "$infile.tmp";

  # retrieve the git hash and date for the main repository
  my ($mysql_git_hash, $mysql_git_date) = git_hash_and_date $root;
  # retrieve the git hash and date for the rocksdb submodule
  my ($rocksdb_git_hash, $rocksdb_git_date) = git_hash_and_date $rocksdb;

  # Open the user's file for reading and a temporary file for writing
  open(my $in, "<", $infile) or die "Could not open $infile";
  open(my $out, ">", $outfile) or die "Could not create $outfile";

  # For each line, see if we can replace anything
  while (<$in>) {
    s/\@MYSQL_GIT_HASH\@/$mysql_git_hash/g;
    s/\@MYSQL_GIT_DATE\@/$mysql_git_date/g;
    s/\@ROCKSDB_GIT_HASH\@/$rocksdb_git_hash/g;
    s/\@ROCKSDB_GIT_DATE\@/$rocksdb_git_date/g;
    print $out $_;
  }

  # Close both files
  close $in;
  close $out;

  # Copy the temporary file to the original and then delete it
  copy $outfile, $infile or die "Unable to copy temp file on top of original";
  unlink $outfile;
}

main(@ARGV);
