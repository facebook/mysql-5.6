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

package GenTest::Comparator;

use strict;

use GenTest;
use GenTest::Constants;
use GenTest::Result;

#
# In order to compare two data sets that may be sorted differently, we convert each row into a string,
# then sort the rows and convert them into one gigantic string. Such string representation is no longer
# dependent on sort order so can be compared safely.
#
# A O(N) algorithm that would avoid sorting is to use a hash representation of each data set. e.g. $hash{"A,B,C"} = 2 
# if there were two rows containing "A,B,C". If two hashes contain the same keys with the same values, the two initial
# data sets were identical
#

1;

sub compare {
	my @resultsets = @_;

	return STATUS_OK if $#resultsets == 0;

	return STATUS_WONT_HANDLE if $resultsets[0]->status() == STATUS_WONT_HANDLE || $resultsets[1]->status() == STATUS_WONT_HANDLE;
	return STATUS_SKIP if $resultsets[0]->status() == STATUS_SKIP || $resultsets[1]->status() == STATUS_SKIP;

	foreach my $i (0..($#resultsets-1)) {

		my $resultset1 = $resultsets[$i];
		my $resultset2 = $resultsets[$i+1];
		if ($resultset1->status() != $resultset2->status()) {
			return STATUS_ERROR_MISMATCH;
		} elsif (
			(not defined $resultset1->data()) &&					# Only for DML statements
			($resultset1->affectedRows() != $resultset2->affectedRows())
		) {
			return STATUS_LENGTH_MISMATCH;
		} else {
			my $data1 = $resultset1->data();
			my $data2 = $resultset2->data();
			return STATUS_LENGTH_MISMATCH if $#$data1 != $#$data2;
			my $data1_sorted = join('<row>', sort map { join('<col>', map { defined $_ ? ($_ != 0 ? sprintf("%.4f", $_) : $_) : 'NULL' } @$_) } @$data1);
			my $data2_sorted = join('<row>', sort map { join('<col>', map { defined $_ ? ($_ != 0 ? sprintf("%.4f", $_) : $_) : 'NULL' } @$_) } @$data2);
			return STATUS_CONTENT_MISMATCH if $data1_sorted ne $data2_sorted;
		}
	}
	return STATUS_OK;
}

sub dumpDiff {
	my @results = @_;
	my @files;
	my $diff;

	foreach my $i (0..1) {
		return undef if not defined $results[$i]->data();
		my $data_sorted = join("\n", sort map { join("\t", map { defined $_ ? $_ : "NULL" } @$_) } @{$results[$i]->data()});
		$data_sorted = $data_sorted."\n" if $data_sorted ne '';
		$files[$i] = tmpdir()."/randgen".$$."-".time()."-server".$i.".dump";
		open (FILE, ">".$files[$i]);
		print FILE $data_sorted;
		close FILE;
	}
	
	my $diff_cmd = "diff -u $files[0] $files[1]";

	open (DIFF, "$diff_cmd|");
	while (<DIFF>) {
		$diff .= $_;
	}
	close DIFF;

	foreach my $file (@files) {
		unlink($file);
	}
	
	return $diff;
	
}

1;
