# Copyright (C) 2008-2009 Sun Microsystems, Inc. All rights reserved.
# Copyright (c) 2013, Monty Program Ab.
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

package GenTest::Reporter::RecoveryCloneCmd;

require Exporter;
@ISA = qw(GenTest::Reporter::Recovery);

use strict;
use DBI;
use GenTest;
use GenTest::Constants;
use GenTest::Reporter;
use GenTest::Reporter::Recovery;
use GenTest::Comparator;
use Data::Dumper;
use IPC::Open2;
use POSIX;

use DBServer::MySQL::MySQLd;

# constructor - set clone mysqld mode

sub new {
        my ($class, @args) = @_;
        my $reporter;
        unless (@args) {
        	$reporter=$class->SUPER::new();
        } else {
        	$reporter=$class->SUPER::new(@args);
        }
        bless($reporter,$class);

        $reporter->cloneCmd(1);

	return $reporter;
};

sub cloneCmd {
        my $reporter = shift;
        if (@_) {
                $reporter->SUPER::cloneCmd(shift);
        }
        return $reporter->SUPER::cloneCmd();
}

sub monitor {
        my $reporter = shift;
        return $reporter->SUPER::monitor();
}

sub report {
        my $reporter = shift;
        return $reporter->SUPER::report();
}
        
1;
