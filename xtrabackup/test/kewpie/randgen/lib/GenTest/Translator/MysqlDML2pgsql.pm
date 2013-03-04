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

## Pgsql/Derby specific variants to MysqlDML2ANSI

package GenTest::Translator::MysqlDML2pgsql;

@ISA = qw(GenTest::Translator::MysqlDML2ANSI GenTest::Translator GenTest);

use GenTest;

use strict;

## LIMIT n is equal
## LIMIT n OFFSET m is equal
## LIMIT m,n needs to be changed

sub limit {
    my $dml = $_[1];
    $dml =~ s/\bLIMIT\s+(\d+)\s*,\s*(\d+)/LIMIT \2 OFFSET \1/;
    return $dml;
}



1;
