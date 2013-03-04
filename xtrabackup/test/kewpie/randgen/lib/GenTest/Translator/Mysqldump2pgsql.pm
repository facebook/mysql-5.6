# Copyright (C) 2009 Sun Microsystems, Inc. All rights reserved.
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

## Postgres specific variants to Mysqldump2ANSI

package GenTest::Translator::Mysqldump2pgsql;

@ISA = qw(GenTest::Translator::Mysqldump2ANSI GenTest::Translator GenTest);

use strict;

sub auto_increment {
    my $line = $_[1];
    ## Assumption types like int(11) etc has been converted to integer
    $line =~ s/\binteger(.*)auto_increment\b/SERIAL \1/i;
    return $line;
}

sub create_index {
    my $iname = $_[1];
    my $table = $_[2];
    my $index = $_[3];
    return "CREATE INDEX $iname ON $table $index;";
}

1;
