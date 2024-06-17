##
## We use our own Runner class for better reporting and exit status
## that works with Hudson.
##
##

package RQGRunner;
use strict;

use base qw(Test::Unit::Runner); 

use Carp;
use Test::Unit; # for copyright & version number
use Test::Unit::TestSuite;
use Test::Unit::Loader;
use Test::Unit::Result;

use Data::Dumper;
use Time::HiRes;

use Benchmark;

my $exit_code = 0;

sub new {
    my $class = shift;
    my ($filehandle) = @_;
    $filehandle = \*STDOUT unless $filehandle;
    select((select($filehandle), $| = 1)[0]);
    bless { _Print_stream => $filehandle }, $class;
}

sub print_stream {
    my $self = shift;
    return $self->{_Print_stream};
}

sub _print {
    my $self = shift;
    my (@args) = @_;
    $self->print_stream->print(@args);
}

sub add_error {
    my $self = shift;
    my ($test, $exception) = @_; 
    my $tn = ref $test;
    $self->_print("Error: ".$tn."::".$test->name()."\n");
    testngReport($tn,$test->name(),"ERROR");
}
	
sub add_failure {
    my $self = shift;
    my ($test, $exception) = @_;
    my $tn = ref $test;
    $self->_print("Failure: ".$tn."::".$test->name()."\n");
    testngReport($tn,$test->name(),"FAIL");
}

sub add_pass {
    my $self = shift;
    my ($test, $exception) = @_;
    my $tn = ref $test;
    $self->_print("Success: ".$tn."::".$test->name()."\n");
    testngReport($tn,$test->name(),"PASS");
}

sub do_run {
    my $self = shift;
    my ($suite, $wait) = @_;
    my $result = $self->create_test_result();
    $result->add_listener($self);
    my $start_time = new Benchmark();
    $suite->run($result, $self);
    my $end_time = new Benchmark();
    
    $self->print_result($result, $start_time, $end_time);
    
    if ($wait) {
        print "<RETURN> to continue"; # go to STDIN any case
        <STDIN>;
    }

    $self->_print("\nTest was not successful.\n")
      unless $result->was_successful;

    return $result->was_successful;
}

sub end_test {
    my $self = shift;
    my ($test) = @_;
    testngEnd($test);
}

sub main {
    my $self = shift;
    my $a_test_runner = Test::Unit::TestRunner->new();
    $a_test_runner->start(@_);
}

sub print_result {
    my $self = shift;
    my ($result, $start_time, $end_time) = @_;

    my $run_time = timediff($end_time, $start_time);
    $self->_print("\n", "Time: ", timestr($run_time), "\n");

    $self->print_header($result);
    $self->print_errors($result);
    $self->print_failures($result);

    testngOutput();
}

sub print_errors {
    my $self = shift;
    my ($result) = @_;
    return unless my $error_count = $result->error_count();
    my $msg = "\nThere " .
              ($error_count == 1 ?
                "was 1 error"
              : "were $error_count errors") .
              ":\n";
    $self->_print($msg);

    my $i = 0;
    for my $e (@{$result->errors()}) {
        chomp(my $e_to_str = $e);
        $i++;
        $self->_print("$i) $e_to_str\n");
        $self->_print("\nAnnotations:\n", $e->object->annotations())
          if $e->object->annotations();
    }
}

sub print_failures {
    my $self = shift;
    my ($result) = @_;
    return unless my $failure_count = $result->failure_count;
    my $msg = "\nThere " .
              ($failure_count == 1 ?
                "was 1 failure"
              : "were $failure_count failures") .
              ":\n";
    $self->_print($msg);

    my $i = 0; 
    for my $f (@{$result->failures()}) {
        chomp(my $f_to_str = $f);
        $self->_print("\n") if $i++;
        $self->_print("$i) $f_to_str\n");
        $self->_print("\nAnnotations:\n", $f->object->annotations())
          if $f->object->annotations();
    }
}

sub print_header {
    my $self = shift;
    my ($result) = @_;
    if ($result->was_successful()) {
        $exit_code = 0;
        $self->_print("\n", "OK", " (", $result->run_count(), " tests)\n");
    } else {
        $exit_code = 1;
        $self->_print("\n", "!!!FAILURES!!!", "\n",
                      "Test Results:\n",
                      "Run: ", $result->run_count(), 
                      ", Failures: ", $result->failure_count(),
                      ", Errors: ", $result->error_count(),
                      "\n");
    }
}

sub run {
    my $self = shift;
    my ($class) = @_;
    my $a_test_runner = Test::Unit::TestRunner->new();
    $a_test_runner->do_run(Test::Unit::TestSuite->new($class), 0);
}
	
sub run_and_wait {
    my $self = shift;
    my ($test) = @_;
    my $a_test_runner = Test::Unit::TestRunner->new();
    $a_test_runner->do_run(Test::Unit::TestSuite->new($test), 1);
}

sub start {
    my $self = shift;
    my (@args) = @_;

    my $test = "";
    my $wait = 0;

    for (my $i = 0; $i < @args; $i++) {
        if ($args[$i] eq "-wait") {
            $wait = 1;
        } elsif ($args[$i] eq "-v") {
	    print Test::Unit::COPYRIGHT_SHORT;
        } else {
            $test = $args[$i];
        }
    }
    if ($test eq "") {
        croak "Usage: TestRunner.pl [-wait] name, where name is the name of the Test class\n";
    }
    
    my $suite = Test::Unit::Loader::load($test);
    $self->do_run($suite, $wait);
    return $exit_code;
}

sub start_test {
    my $self = shift;
    my ($test) = @_;
    testngStart($test);
}

my %testng_method;
my %testng_class;
my %testng_start;
my %testng_end;

sub isoTs {
    my ($arg) = @_;
    my ($sec, $min, $hour, $mday, $mon, $year, $wday, $yday, $isdst) = gmtime(int($arg));
    return sprintf("%04d-%02d-%02dT%02d:%02d:%02dZ", $year+1900, $mon+1 ,$mday ,$hour, $min, $sec);

}
sub testngReport {
    my ($class,$method,$result) = @_;
    $testng_method{$class."::".$method} = $result;
    $testng_class{$class}=1;
}

sub testngStart() {
    my ($test) = @_;
    my $tn = ref $test;
    $testng_start{$tn."::".$test->name()}=Time::HiRes::time();
}

sub testngEnd() {
    my ($test) = @_;
    my $tn = ref $test;
    $testng_end{$tn."::".$test->name()}=Time::HiRes::time();
}

sub testngOutput {
    open TESTNG,">unit/rqg.testng.xml";
    print TESTNG "<testng-results>\n";
    print TESTNG "  <test name=\"RQGunit\">\n";
    foreach my $c (keys %testng_class) {
        print TESTNG "    <class name=\"RQG.unit.$c\">\n";
        foreach my $x (keys %testng_method) {
            my ($class,$method) = $x =~ m/(.*)::(.*)/;
	    my $dur = int(($testng_end{$x}-$testng_start{$x})*1000);
            if ($class eq $c) {
                print TESTNG "      <test-method name=\"$method\" status=\"$testng_method{$x}\"\n";
                print TESTNG "                   signature=\"".$method."()\"\n";
		print TESTNG "                   duration-ms=\"$dur\"\n";
                print TESTNG "                   started-at=\"".isoTs($testng_start{$x})."\"\n";
                print TESTNG "                   finished-at=\"".isoTs($testng_end{$x})."\">\n";
                print TESTNG "      </test-method>\n";
            }
        }
        print TESTNG "    </class>\n";
    }
    print TESTNG "  </test>\n";
    print TESTNG "</testng-results>\n";
    close TESTNG;
}
1;
__END__


=head1 NAME

Test::Unit::TestRunner - unit testing framework helper class

=head1 SYNOPSIS

    use Test::Unit::TestRunner;

    my $testrunner = Test::Unit::TestRunner->new();
    $testrunner->start($my_test_class);

=head1 DESCRIPTION

This class is the test runner for the command line style use
of the testing framework.

It is used by simple command line tools like the F<TestRunner.pl>
script provided.

The class needs one argument, which is the name of the class
encapsulating the tests to be run.

=head1 OPTIONS

=over 4

=item -wait

wait for user confirmation between tests

=item -v

version info

=back


=head1 AUTHOR

Copyright (c) 2000-2002, 2005 the PerlUnit Development Team
(see L<Test::Unit> or the F<AUTHORS> file included in this
distribution).

All rights reserved. This program is free software; you can
redistribute it and/or modify it under the same terms as Perl itself.

=head1 SEE ALSO

=over 4

=item *

L<Test::Unit::TestCase>

=item *

L<Test::Unit::Listener>

=item *

L<Test::Unit::TestSuite>

=item *

L<Test::Unit::Result>

=item *

L<Test::Unit::TkTestRunner>

=item *

For further examples, take a look at the framework self test
collection (t::tlib::AllTests).

=back

=cut
