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

package GenTest::Validator::DatabaseComparator;

require Exporter;
@ISA = qw(GenTest GenTest::Validator);

use strict;

use Data::Dumper;
use GenTest;
use GenTest::Constants;
use GenTest::Result;
use GenTest::Validator;

use constant SERVER1_FILE_NAME  => 0;
use constant SERVER2_FILE_NAME  => 1;

sub validate {
	my ($validator, $executors, $results) = @_;

	my @files;
	my @ports = ('19306', '19308');

	my $database = $executors->[0]->currentSchema();

	foreach my $port_id (0..1) {
		$files[$port_id] = tmpdir()."/dump_".$$."_".$ports[$port_id].".sql";
		my $mysqldump_result = system("mysqldump --compact --order-by-primary --skip-triggers --skip-extended-insert --no-create-info --host=127.0.0.1 --port=$ports[$port_id] --user=root $database | sort > $files[$port_id]");
		return STATUS_UNKNOWN_ERROR if $mysqldump_result > 0;
	}

	my $diff_result = system("diff --unified $files[SERVER1_FILE_NAME] $files[SERVER2_FILE_NAME]");
	$diff_result = $diff_result >> 8;
	return STATUS_UNKNOWN_ERROR if $diff_result > 1;

	if ($diff_result == 1) {
		say("Differences between the two servers were found after query ".$results->[0]->query());
		return STATUS_REPLICATION_FAILURE;
	} else {
		foreach my $file (@files) {
			unlink($file);
		}
		return STATUS_OK;
	}
}

sub prerequsites {
	return ['ReplicationWaitForSlave'];
}

1;
