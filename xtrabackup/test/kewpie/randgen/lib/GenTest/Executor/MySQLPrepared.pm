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

package GenTest::Executor::MySQLPrepared;

require Exporter;

@ISA = qw(GenTest::Executor GenTest::Executor::MySQL);

use strict;
use DBI;
use GenTest;
use GenTest::Constants;
use GenTest::Result;
use GenTest::Executor;
use GenTest::Executor::MySQL;

1;

sub execute {
	my ($executor, $query) = @_;

	my $statement_id = 'statement'.abs($$);
	my $prepare_result = GenTest::Executor::MySQL::execute($executor, "PREPARE $statement_id FROM '$query'");
	return $prepare_result if $prepare_result->status() > STATUS_OK;
	
	my $execute_result = $executor->SUPER::execute("EXECUTE $statement_id");
	$execute_result->[GenTest::Result::RESULT_QUERY] = $query;

	$executor->SUPER::execute("DEALLOCATE PREPARE $statement_id");
		
	return $execute_result;
}

1;
