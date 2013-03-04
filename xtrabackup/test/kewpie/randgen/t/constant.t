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

use Test::More tests => 3;

ok(constant2text(ORACLE_ISSUE_NO_LONGER_REPEATABLE, 'ORACLE_') eq 'ORACLE_ISSUE_NO_LONGER_REPEATABLE', 'constant2text');
ok(! defined constant2text('foo','bar'), 'constant2text_nonexisting');
ok(status2text(STATUS_SERVER_CRASHED) eq 'STATUS_SERVER_CRASHED', 'status2text');
