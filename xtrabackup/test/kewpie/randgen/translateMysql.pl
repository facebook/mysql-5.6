# Copyright (C) 2009 Sun Microsystems, Inc. All rights reserved.  Use
# is subject to license terms.
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

#!/usr/bin/perl

use lib 'lib';
use lib "$ENV{RQG_HOME}/lib";
use strict;
use GenTest;
use GenTest::Translator;
use GenTest::Translator::MysqlDML2ANSI;
use GenTest::Translator::MysqlDML2pgsql;
use GenTest::Translator::MysqlDML2javadb;
use GenTest::Translator::Mysqldump2ANSI;
use GenTest::Translator::Mysqldump2pgsql;
use GenTest::Translator::Mysqldump2javadb;

use Getopt::Long;

my $from = "unspecified";
my $to = "ansi";

my $opt_result = GetOptions(
    'from=s' => $from,
    'to=s' => \$to
    );

$from =~ tr/A-Z/a-z/;
$to =~ tr/A-Z/a-z/;

my $translator1;
my $translator2;
if ($to eq "ansi") {
    $translator1 = GenTest::Translator::Mysqldump2ANSI->new();
    $translator2 = GenTest::Translator::MysqlDML2ANSI->new();
} elsif ($to eq "javadb" || $to eq "derby") {
    $translator1 = GenTest::Translator::Mysqldump2javadb->new();
    $translator2 = GenTest::Translator::MysqlDML2javadb->new();
} elsif ($to eq "postgres" || $to eq "pg" || $to eq "postgresql" || $to eq "pgsql") {
    $translator1 = GenTest::Translator::Mysqldump2pgsql->new();
    $translator2 = GenTest::Translator::MysqlDML2pgsql->new();
} else {
    die "Unknown target \"$to\", use \"ansi\", \"javadb\" or \"postgresql\"";
}

my $file;
while(<>) {
    $file .= $_;
}

my $result;

if ($from eq "unspecified") {
    $result = $translator1->translate($file);
    $result = $translator2->translate($result);
} elsif ($from eq "mysqldump") {
    $result = $translator1->translate($file);
} elsif ($from eq "dml") {
    $result = $translator2->translate($file);
} else {
    die "Unknown source \"$from\", if specified, use \"mysqldump\" or \"dml\"";
}

print $result;


