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

my $mysql_git_hash = "";
my $mysql_git_date = "";
my $rocksdb_git_hash = "";
my $rocksdb_git_date = "";

# Simple command line processing that expects just two items
#  1) a subdirectory
#  2) a file name
sub process_cmd_line {
  my $root = "";
  my $infile = "";
  my $bitmask = 0;
  my $help = "
usage:
  $0 {options}

where options are
  --git_root=<path>
  --file=<file>
  [--mysql_githash=<hash>]
  [--mysql_gitdate=<date>]
  [--rocksdb_githash=<hash>]
  [--rocksdb_gitdate=<date>]
";

  # Loop through the command line arguments
  foreach my $option (@_) {
    if ($option =~ /^--git_root/)
    {
      $root = substr($option, 11);
      next;
    }
    if ($option =~ /^--file/)
    {
      $infile = substr($option, 7);
      next;
    }
    if ($option =~ /^--mysql_githash/)
    {
      $bitmask = $bitmask | 1;
      $mysql_git_hash = substr($option, 16);
      next;
    }
    if ($option =~ /^--mysql_gitdate/)
    {
      $bitmask = $bitmask | 2;
      $mysql_git_date = substr($option, 16);
      next;
    }
    if ($option =~ /^--rocksdb_githash/)
    {
      $bitmask = $bitmask | 4;
      $rocksdb_git_hash = substr($option, 18);
      next;
    }
    if ($option =~ /^--rocksdb_gitdate/)
    {
      $bitmask = $bitmask | 8;
      $rocksdb_git_date = substr($option, 18);
      next;
    }

    die "Invalid option: $option\n$help";
  }

  die "If you specify any of the git hashes or dates you must specify them all"
      unless ($bitmask == 0 || $bitmask == 15);

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
  my $git_cmd = "git log -1 --format=\"%H;%cI\"";
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

  if ($mysql_git_hash eq "") {
    # retrieve the git hash and date for the main repository
    ($mysql_git_hash, $mysql_git_date) = git_hash_and_date $root;
    # retrieve the git hash and date for the rocksdb submodule
    ($rocksdb_git_hash, $rocksdb_git_date) = git_hash_and_date $rocksdb;
  }

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
