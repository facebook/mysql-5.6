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

use Test::More tests => 8;

use GenTest::Random;

my $prng = GenTest::Random->new(
	seed => 2
);

my $numbers = join(' ', map { $prng->digit() } (0..9));
ok($numbers eq '7 4 9 8 4 1 2 1 5 2', 'prng_stability');
ok($prng->date() eq '2000-07-14', 'prng_date');

ok($prng->string() eq 'w', 'prng_string_empty');
ok($prng->string(0) eq '', 'prng_string_empty');
ok($prng->string(1) eq 'a', 'prng_string_one');
ok($prng->string(20) eq 'qccmdluyolx', 'prng_string_twenty1');
ok($prng->string(20) eq 'ccmdluyolx', 'prng_string_twenty2');
ok(length($prng->string(65535)) > 1024, 'prng_string_huge');
