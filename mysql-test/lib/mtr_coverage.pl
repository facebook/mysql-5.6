use strict;

# Extract coverage option
#
# Arguments:
#  $option       coverage option
#  $delim        delimiter for splitting the string
#  $option_error error to be printed in case of failure
sub coverage_extract_option {
  my ($option, $delim, $option_error) = @_;
  # check for sanity of option which should be of the format --option=value
  if (length($option) == 0) {
      print "**** ERROR **** ",
            "Invalid coverage option specified for $option_error\n";
      exit(1);
  }

  # split the string on delimiter '='
  my @option_arr = split(/$delim/, $option);
  $option=$option_arr[$#option_arr];

  return $option;
}

# Prepare to generate coverage data
#
# Arguments:
#   $dir       basedir, normally the build directory
#   $scope     coverage option which is of the form:
#                * full : complete code coverage
#                * diff : coverage of the git diff HEAD
#                * diff:<commit_hash> : coverage of git diff commit_hash
#   $src_path  directory path for coverage source files
#   $llvm_path directory for llvm coverage binaries
#   $format    format for coverage report which is of the form:
#                * text : text format
#                * html : html format
sub coverage_prepare($$) {
  my ($dir, $scope, $src_path, $llvm_path, $format) = @_;

  print "Purging coverage information from '$dir'...\n";
  system("find $dir -name \"code\*.profraw\" | xargs rm");

  my $scope = coverage_extract_option($scope, "=", "coverage-scope");

  my $commit_hash = "HEAD"; # default commit hash is 'HEAD'
  # if the coverage scope is "--full" then extract the git commithash
  if ($scope =~ m/^diff/) {
      my $invalid_commit_hash = 0; # is this commit hash valid?
      # if the coverage scope is of the form 'diff:<commit_hash>'
      if ($scope =~ /^diff:/) {
          $commit_hash = coverage_extract_option($scope, ":",
                                                 "coverage-scope");
          # sanity check for commit hash
          if (length($commit_hash) == 0) {
              $invalid_commit_hash = 1;
          }
      }
      # if the coverage scope is of the form '--diff'
      elsif ($scope ne "diff") {
          $invalid_commit_hash = 1;
      }

      if ($invalid_commit_hash) {
          print "**** ERROR **** ",
                "Invalid coverage scope diff option: $scope\n";
          exit(1);
      }
  }
  # make sure that the coverage scope is "--full"
  elsif ($scope ne "full") {
    print "**** ERROR **** ", "Invalid coverage scope: $scope\n";
    exit(1);
  }

  # Update the scope of the coverage
  if ($scope eq "full") {
    $_[1] = $scope;
  }
  else {
    $_[1] = $commit_hash;
  }

  # extract directory for coverage source files
  $src_path = coverage_extract_option($src_path, "=", "coverage-src-path")."/";

  # Update the coverage src path
  $_[2] = $src_path;

  $llvm_path = coverage_extract_option($llvm_path, "=", "coverage-llvm-path");

  # append "/" at the end of the llvm_path
  if (length($llvm_path) > 0 && ! ($llvm_path =~ m/\/$/) ) {
    $llvm_path .= "/";
  }

  # Update the coverage llvm path
  $_[3] = $llvm_path;

  # extract format for coverage report
  $format = coverage_extract_option($format, "=", "coverage-format");

  # sanity check for coverage format
  if ( ! ( ($format eq "text") || ($format eq "html")) ) {
      print "**** ERROR **** ", "Invalid coverage-format option: $format\n";
      exit(1);
  }

  $_[4] = $format;
}

# Get the files modified by a git diff
#
# Arguments:
#  $src_dir      directory for coverage source files
#  $commit_hash  git commit hash
sub coverage_get_diff_files ($$) {
  my ($src_dir, $commit_hash) = @_;

  # command to extract files modified by a git commit hash
  my $cmd = "git diff --name-only $commit_hash"."^ $commit_hash";
  open(PIPE, "$cmd|");

  my $commit_hash_files; # concatenated list of files

  while(<PIPE>) {
      chomp;
      if (/\.h$/ or /\.cc$/) {
          $commit_hash_files .= $src_dir.$_." ";
      }
  }
  return $commit_hash_files;
}

# Collect coverage information
#
# Arguments:
#  $test_dir    directory of the tests
#  $binary_path path to mysqld binary
#  $scope       coverage option which is of the form:
#                 * full : complete code coverage
#                 * HEAD : coverage of the git diff HEAD
#                 * <commit_hash> : coverage of git diff commit_hash
#  $src_path    directory path for coverage source files
#  $llvm_path   directory for llvm coverage binaries
#  $format      format for coverage report which is of the form:
#                 * text : text format
#                 * html : html format
sub coverage_collect ($$$) {
  my ($test_dir, $binary_path, $scope, $src_path, $llvm_path, $format) = @_;

  my $files_modified=""; # list of files modified concatenated into one string

  if ($scope ne "full") {
    $files_modified = coverage_get_diff_files($src_path, $scope);
  }

  print "Generating coverage information";
  if ($scope eq "full") {
    print "for complete source code ";
  }
  else {
    print "for git commit hash $scope";
    if ($scope eq "HEAD") {
      # command to extract git commit hash of 'HEAD'
      my $cmd = "git rev-parse $scope";
      open(PIPE, "$cmd|");
      my $head_commit_hash = <PIPE>;
      chomp($head_commit_hash);
      print " ($head_commit_hash)";
    }
  }
  print " ...\n";

  # Create directory to store the coverage results
  my $result_dir = "$test_dir/reports/".time();
  my $mkdir_cmd = "mkdir -p $result_dir";
  system($mkdir_cmd);

  # Recreate the 'last' directory to point to the latest coverage
  # results directory
  my $rm_link_cmd = "rm -f $test_dir/reports/last";
  system($rm_link_cmd);

  my $create_link_cmd = "ln -s $result_dir $test_dir/reports/last";
  system($create_link_cmd);

  # Merge coverage reports using command
  # llvm-prof merge --output=file.profdata <list of code*.profraw>
  my $merge_cov = $llvm_path."llvm-profdata merge --output=";
  $merge_cov .= $result_dir."/combined.profdata ";
  $merge_cov .= "`find $test_dir -name \"code*.profraw\"`";
  system("$merge_cov");

  # Generate coverage report using command
  # llvm-cov show <binary_path> --instr-profile=file.profdata \
  #      --format <text|html> --output-dir=<output_dir>
  my $generate_cov
      = $llvm_path."llvm-cov show $binary_path -instr-profile=";
  $generate_cov .= $result_dir."/combined.profdata ";

  # Add the list of files modified if the coverage report is for
  # a specific git diff
  if (length($files_modified) > 0) {
      $generate_cov .= $files_modified;
  }
  $generate_cov .= "--format $format"; # coverage report format
  $generate_cov .= " --output-dir=$result_dir 2>/dev/null";
  system($generate_cov);

  # Delete profdata file
  my $rm_profdata = "rm -f ".$result_dir."/combined.profdata";
  system($rm_profdata);

  print "Completed generating coverage information in ",
        "$format format.\n";
  print "Coverage results directory: $test_dir/reports/last\n";
}

1;
