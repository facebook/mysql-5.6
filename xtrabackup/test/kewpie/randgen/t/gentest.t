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

use strict;
use lib 'lib';
use lib '../lib';

use GenTest::Constants;
use Test::More tests => 1;

my $exit_code = system("perl gentest.pl --dsn=dbi:mysql:host=127.0.0.1:port=12345:user=foo:database=bar --grammar=t/gensql.yy");
ok (($exit_code >> 8) == STATUS_ENVIRONMENT_FAILURE, 'gentest_baddsn');
