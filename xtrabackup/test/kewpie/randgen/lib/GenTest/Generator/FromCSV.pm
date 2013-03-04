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

package GenTest::Generator::FromCSV;

require Exporter;
@ISA = qw(GenTest::Generator GenTest);

use strict;
use GenTest;
use GenTest::Generator;
use GenTest::Constants;

my @csv_modules = (
        'Text::CSV',
        'Text::CSV_XS',
        'Text::CSV_PP'
);

my $csv_file = '/tmp/queries.CSV';
my $CSV_HANDLE;
my $csv_obj;

sub new {
        my $class = shift;
	my $generator = $class->SUPER::new(@_);

	foreach my $csv_module (@csv_modules) {
		eval ("require $csv_module");
		if (!$@) {
			$csv_obj = $csv_module->new({ 'escape_char' => '\\' });
			say("Loaded CSV module $csv_module");
			last;
		}
	}

	die "Unable to load a CSV Perl module (tried ".join(', ', @csv_modules).")" if not defined $csv_obj;

	open ($CSV_HANDLE, "<", $csv_file) or die "Unable to open $csv_file: $!";

	return $generator;
}

sub next {
	while() {
		my $_ = <$CSV_HANDLE>;
		return undef if not defined $_;

                if ($csv_obj->parse($_)) {
                        my @columns = $csv_obj->fields();
			my $command = $columns[4];
                        my $query = $columns[5];
			next if $command ne 'Query';
			return [ $query ];
		} else {
			my $err = $csv_obj->error_input;
#			say ("Failed to parse line $_: $err");
			next;

		}
	}
}

1;
