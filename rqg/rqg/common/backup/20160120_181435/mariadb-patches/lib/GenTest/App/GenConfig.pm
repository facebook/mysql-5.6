# Copyright (C) 2014 SkySQL Ab
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
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA

package GenTest::App::GenConfig;

@ISA = qw(GenTest);

use strict;
#use DBI;
use Carp;
use GenTest;
#use GenTest::Constants;
#use GenTest::Random;
#use GenTest::Executor;

#use Data::Dumper;

# use constant FIELD_TYPE			=> 0;
# use constant FIELD_CHARSET		=> 1;
# use constant FIELD_COLLATION		=> 2;
# use constant FIELD_SIGN			=> 3;
# use constant FIELD_NULLABILITY		=> 4;
# use constant FIELD_INDEX		=> 5;
# use constant FIELD_AUTO_INCREMENT	=> 6;
# use constant FIELD_SQL			=> 7;
# use constant FIELD_INDEX_SQL		=> 8;
# use constant FIELD_NAME			=> 9;
# use constant FIELD_DEFAULT => 10;

# use constant TABLE_ROW		=> 0;
# use constant TABLE_ENGINE	=> 1;
# use constant TABLE_CHARSET	=> 2;
# use constant TABLE_COLLATION	=> 3;
# use constant TABLE_ROW_FORMAT	=> 4;
# use constant TABLE_PARTITION	=> 5;
# use constant TABLE_PK		=> 6;
# use constant TABLE_SQL		=> 7;
# use constant TABLE_NAME		=> 8;
# use constant TABLE_VIEWS	=> 9;
# use constant TABLE_MERGES	=> 10;
# use constant TABLE_NAMES	=> 11;

# use constant DATA_NUMBER	=> 0;
# use constant DATA_STRING	=> 1;
# use constant DATA_BLOB		=> 2;
# use constant DATA_TEMPORAL	=> 3;
# use constant DATA_ENUM		=> 4;


use constant GC_SPEC => 0;
use constant GC_DEBUG => 1;
use constant GC_CONFIG => 2;
use constant GC_SEED => 3;
# use constant GD_ENGINE => 4;
# use constant GD_ROWS => 5;
# use constant GD_VIEWS => 6;
# use constant GD_VARCHAR_LENGTH => 7;
# use constant GD_SERVER_ID => 8;
# use constant GD_SQLTRACE => 9;
# use constant GD_NOTNULL => 10;
# use constant GD_SHORT_COLUMN_NAMES => 11;
# use constant GD_STRICT_FIELDS => 12;

sub new {
	my $class = shift;
	
	my $self = $class->SUPER::new({
		'spec_file' => GC_SPEC,
		'debug' => GC_DEBUG,
		'seed' => GC_SEED},@_);

	if (not defined $self->[GC_SEED]) {
		$self->[GC_SEED] = 1;
	} elsif ($self->[GC_SEED] eq 'time') {
		$self->[GC_SEED] = time();
		say("GenConfig: Converting --seed=time to --seed=".$self->[GC_SEED]);
	}
	
	return generate($self);
}


sub spec_file {
return $_[0]->[GC_SPEC];
}


sub debug {
	return $_[0]->[GC_DEBUG];
}


sub seed {
	return $_[0]->[GC_SEED];
}

sub rand_bool {
	return int(rand(2));
}

sub rand_int {
	my ($first, $last) = @_;
	return int(rand($last-$first+1)) + $first;
}

sub rand_float {
	my ($first, $last) = @_;
	return rand($last-$first+0.0001) + $first;
}

sub rand_array_element {
	return @_[rand_int(0,$#_)];
}

sub generate {
	my ($self) = @_;

	my $spec_file = $self->spec_file();
	
	return undef if $spec_file eq '';
	open(TEMPLATE, "<$spec_file") or croak "ERROR: GenConfig: unable to open specification file '$spec_file': $!";
	my @config_contents = ();

	srand($self->seed());

	while (<TEMPLATE>) 
	{
		if  (/^\s*([^\#\s]+)(?:\s*=\s*)?\s*\[\s*(.*)\s*\](.*)/) {
			# real option with template
			my ( $name, $template, $suffix ) = ( $1, $2 , $3 );
			next if (! rand_bool()); # do not use the template, leave default;

			if ($template =~ /\(\s*(\S+)\s*\)/) {

				# Should be a comma-separated list which presents a set -- 
				# several values can be used simultaneously
				my $allowed_values = $1;
				my %allowed_values = map( ($_, 1), split /,/, $allowed_values);
				my $number_of_values = rand_int(0,scalar(keys %allowed_values));
				my @values = ();
				foreach (1 .. $number_of_values) {
					my $val = rand_array_element(keys %allowed_values);
					push @values, $val;
					delete $allowed_values{$val};
				}
				my $values = "'" . ( join ',', @values ) . "'";
				push @config_contents, sprintf( "loose-%-64s = $values\n" , $name );
			}
		
			elsif ($template =~ /\s,\s/) {
				# Should be a comma-separated list which presents a single-choice
				# the comma is supposed to be surrounded by spaces, to differentiate
				# from actual values containing commas
				my @allowed_values = split / , /, $template;
				push @config_contents, sprintf( "loose-%-64s = " . rand_array_element(@allowed_values) . "\n" , $name );
			}

			elsif ($template =~ /([\.\d]+)\s+..\s+([\.\d]+)/) {
				# Should be a numeric range, either of integer or float values.
				# The first and last limits are separated by ' .. ' (with spaces).
				my ($first, $last) = ($1, $2);
				if ( $first =~ /\./ or $last =~ /\./ ) {
					push @config_contents, sprintf( "loose-%-64s = " . rand_float($first,$last) . "\n" , $name );
				}
				else {
					push @config_contents, sprintf( "loose-%-64s = " . rand_int($first,$last) . "\n" , $name );
				}
			}

		}
		elsif (/^\s*\#/) {
			# Comment lines, ignore for now
		}
		else {
			# Something else: "hardcoded" option, section start, empty line..
			push @config_contents, $_;
		}
	}

	return \@config_contents;
}

1;
