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

package GenTest::Generator::FromDirectory;

require Exporter;
@ISA = qw(GenTest::Generator GenTest);

use strict;
use GenTest::Constants;
use GenTest::Generator;

use constant GENERATOR_DIRECTORY_NAME	=> 0;
use constant GENERATOR_DIRECTORY_HANDLE	=> 1;

sub new {
        my $class = shift;

        my $generator = $class->SUPER::new({
		'directory_name'	=> GENERATOR_DIRECTORY_NAME
	}, @_ );

	opendir($generator->[GENERATOR_DIRECTORY_HANDLE], $generator->[GENERATOR_DIRECTORY_NAME]) or die("Unable to open directory $generator->[GENERATOR_DIRECTORY_NAME]: $!");
	return $generator;
}

sub next {
	my $generator = shift;

	while() {
		my $next_file = readdir($generator->[GENERATOR_DIRECTORY_HANDLE]);
		if (($next_file eq '.') || ($next_file eq '..')) {
			next;
		} elsif (not defined $next_file) {
			closedir($generator->[GENERATOR_DIRECTORY_HANDLE]);
			return undef;	# All files have been read
		} else {
			my $next_file = $generator->[GENERATOR_DIRECTORY_NAME].'/'.$next_file;
			open(QUERYFILE, $next_file) or die("Unable to open $next_file: $!");
			read(QUERYFILE, my $query , -s $next_file);
			chomp($query);
			return [ $query ];
		}
	}
}

1;
