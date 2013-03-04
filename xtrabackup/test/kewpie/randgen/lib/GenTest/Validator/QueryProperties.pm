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

package GenTest::Validator::QueryProperties;

require Exporter;
@ISA = qw(GenTest::Validator GenTest);

use strict;

use GenTest;
use Data::Dumper;
use GenTest::Constants;
use GenTest::Result;
use GenTest::Validator;

my @properties = (
	'RESULTSET_HAS_SAME_DATA_IN_EVERY_ROW',
	'RESULTSET_IS_SINGLE_INTEGER_ONE',
	'RESULTSET_HAS_ZERO_OR_ONE_ROWS',
	'RESULTSET_HAS_ONE_ROW',
	'QUERY_IS_REPLICATION_SAFE'
);

my %properties;
foreach my $property (@properties) {
	$properties{$property} = 1;
};

sub validate {
	my ($validator, $executors, $results) = @_;

	my $query = $results->[0]->query();
	my @query_properties = $query =~ m{((?:RESULTSET_|ERROR_|QUERY_).*?)[^A-Z_0-9]}sog;

	return STATUS_WONT_HANDLE if $#query_properties == -1;

	my $query_status = STATUS_OK;

	foreach my $result (@$results) {
		foreach my $query_property (@query_properties) {
			my $property_status = STATUS_OK;
			if (exists $properties{$query_property}) {
				#
				# This is a named property, call the respective validation procedure
				#
				$property_status = $validator->$query_property($result);
			} elsif (my ($error) = $query_property =~ m{ERROR_(.*)}so) {
				#
				# This is an error code, check that the query returned that error code
				#

				if ($error !~ m{^\d*$}) {
					say("Query: $query needs to use a numeric code in in query property $query_property.");
					return STATUS_ENVIRONMENT_FAILURE;
				} elsif ($result->err() != $error) {
					say("Query: $query did not fail with error $error.");
					$property_status = STATUS_ERROR_MISMATCH;
				}
			}
			$query_status = $property_status if $property_status > $query_status;
		}
	}

	if ($query_status != STATUS_OK) {
		say("Query: $query does not have the declared properties: ".join(', ', @query_properties));
		print Dumper $results if rqg_debug();
	}
	
	return $query_status;
}


sub RESULTSET_HAS_SAME_DATA_IN_EVERY_ROW {
	my ($validator, $result) = @_;

	return STATUS_OK if not defined $result->data();
	return STATUS_OK if $result->rows() < 2;

	my %data_hash;
	foreach my $row (@{$result->data()}) {
		my $data_item = join('<field>', @{$row});
		$data_hash{$data_item}++;
	}
	
	if (keys(%data_hash) > 1) {
		return STATUS_CONTENT_MISMATCH;
	} else {
		return STATUS_OK;
	}
}

sub RESULTSET_HAS_ZERO_OR_ONE_ROWS {
	my ($validator, $result) = @_;
	
	if ($result->rows() > 1) {
		return STATUS_LENGTH_MISMATCH;
	} else {
		return STATUS_OK;
	}
}

sub RESULTSET_HAS_ONE_ROW {
	my ($validator, $result) = @_;
	
	if ($result->rows() != 1) {
		return STATUS_LENGTH_MISMATCH;
	} else {
		return STATUS_OK;
	}
}

sub RESULTSET_IS_SINGLE_INTEGER_ONE {
	my ($validator, $result) = @_;
	
	if (
		(not defined $result->data()) ||
		($#{$result->data()} != 0) ||
		($result->rows() != 1) ||
		($#{$result->data()->[0]} != 0) ||
		($result->data()->[0]->[0] != 1)
	) {
		return STATUS_CONTENT_MISMATCH;
	} else {
		return STATUS_OK;
	}
}

sub QUERY_IS_REPLICATION_SAFE {

	my ($validator, $result) = @_;

	my $warnings = $result->warnings();

	if (defined $warnings) {
		foreach my $warning (@$warnings) {
			return STATUS_ENVIRONMENT_FAILURE if $warning->[1] == 1592;
		}
	}
	return STATUS_OK;
}

1;
