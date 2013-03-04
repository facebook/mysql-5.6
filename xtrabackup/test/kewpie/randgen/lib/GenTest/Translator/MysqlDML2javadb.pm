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

## Javadb/Derby specific variants to MysqlDML2ANSI

package GenTest::Translator::MysqlDML2javadb;

@ISA = qw(GenTest::Translator::MysqlDML2ANSI GenTest::Translator GenTest);

use strict;

use GenTest;

sub supported_join() {
    if ($_[1] =~ m/\bUSING\b/i ) {
        say("USING clause not supported by JavaDB/Derby");
        return 0;
    } elsif ($_[1] =~ m/\bNATURAL\b/i ) {
        say("NATURAL join not supported by JavaDB/Derby");
        return 0;
    } else {
        return 1;
    }
}

1;

