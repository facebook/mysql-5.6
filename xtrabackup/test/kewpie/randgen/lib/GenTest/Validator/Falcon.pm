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

package GenTest::Validator::Falcon;

require Exporter;
@ISA = qw(GenTest::Validator GenTest);

use strict;

use DBI;
use GenTest;
use GenTest::Constants;
use GenTest::Result;
use GenTest::Validator;

1;

sub init {
	my ($validator, $executors) = @_;

#	
# We could use the DBI connection already established by the Executor, however this will mean that
# the Validator SELECT statements will be interleaved with the other statements issued by the Executor.
# If we have transactions, SELECT-ing over entire tables will certainly change the behavoir of the transaction
#

	my $dsn = $executors->[0]->dsn();
	my $dbh = DBI->connect($dsn, undef, undef, { RaiseError => 1 });
        $validator->setDbh($dbh);

	$dbh->do("SET AUTOCOMMIT=ON");

        return 1;

}


sub validate {
	my ($validator, $executors) = @_;

	my $dbh = $validator->dbh();
	my $executor = $executors->[0];
	
	my $tables = $executor->tables();
	foreach my $table (@$tables) {
		my $sth = $dbh->prepare("
			SELECT pk, COUNT(*) AS C
			FROM `$table`
			GROUP BY pk
			HAVING C > 1
		");
		$sth->execute();
		my $rows = $sth->rows();
		$sth->finish();

		if ($rows) {
			$sth->finish();
			say("Table $table contains duplicate primary keys.");
			return STATUS_DATABASE_CORRUPTION;
		}
	}

	return STATUS_OK;
}

1;
