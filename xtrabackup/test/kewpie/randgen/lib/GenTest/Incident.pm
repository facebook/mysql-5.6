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

package GenTest::Incident;

require Exporter;
@ISA = qw(GenTest);

use strict;
use GenTest;

#
# Those names are taken from an XML specification for a 
# test result XML report (see XML/Report.pm). Not all of them will be used.
# See commented tags / statements for omitted stuff.
#

use constant INCIDENT_ID            => 0;
use constant INCIDENT_TIMESTAMP     => 1;
use constant INCIDENT_RESULT        => 2;
use constant INCIDENT_DESCRIPTION   => 3;   
use constant INCIDENT_SIGNATURE     => 4;
use constant INCIDENT_COREFILE      => 5;
use constant INCIDENT_ANALYSIS      => 6;
use constant INCIDENT_CLASS         => 7;
use constant INCIDENT_BUG_ID        => 8;
use constant INCIDENT_DEBUGS        => 9;

use constant INCIDENT_DEBUG_TYPE    => 0;
use constant INCIDENT_DEBUG_TEXT    => 1;

my $id = 0;

1;

sub new {
    my $class = shift;

    my $incident = $class->SUPER::new({
        id          => INCIDENT_ID,
        timestamp   => INCIDENT_TIMESTAMP,
        result      => INCIDENT_RESULT,
        description => INCIDENT_DESCRIPTION,
        signature   => INCIDENT_SIGNATURE,
        corefile    => INCIDENT_COREFILE,
        analysis    => INCIDENT_ANALYSIS,
        class       => INCIDENT_CLASS,
        bug_id      => INCIDENT_BUG_ID,
        debugs      => INCIDENT_DEBUGS
    }, @_);

    $incident->[INCIDENT_TIMESTAMP] = isoUTCTimestamp() if not defined $incident->[INCIDENT_TIMESTAMP];
    $incident->[INCIDENT_ID] = $id++ if not defined $incident->[INCIDENT_ID];

    return $incident;
}

sub xml {
    require XML::Writer;

    my $incident = shift;
    my $incident_xml;

    my $writer = XML::Writer->new(
        OUTPUT      => \$incident_xml,
        DATA_MODE   => 1,   # this and DATA_INDENT to have line breaks and indentation after each element
        DATA_INDENT => 2,   # number of spaces used for indentation
        UNSAFE      => 1
    );

    $writer->startTag('incident', 'id' => $incident->[INCIDENT_ID]);

    # this is a sequence in the XML schema, so the order of elements is significant.
    $writer->dataElement('timestamp', $incident->[INCIDENT_TIMESTAMP]) if defined $incident->[INCIDENT_TIMESTAMP];
    $writer->dataElement('result', $incident->[INCIDENT_RESULT]) if defined $incident->[INCIDENT_RESULT];
    $writer->dataElement('description', $incident->[INCIDENT_DESCRIPTION]) if defined $incident->[INCIDENT_DESCRIPTION];
    $writer->cdataElement('signature', $incident->[INCIDENT_SIGNATURE]) if defined $incident->[INCIDENT_SIGNATURE];
    $writer->dataElement('corefile', $incident->[INCIDENT_COREFILE]) if defined $incident->[INCIDENT_COREFILE];
    $writer->dataElement('analysis', $incident->[INCIDENT_ANALYSIS]) if defined $incident->[INCIDENT_ANALYSIS];
    $writer->dataElement('class', $incident->[INCIDENT_CLASS]) if defined $incident->[INCIDENT_CLASS];
    $writer->dataElement('bug_id', $incident->[INCIDENT_BUG_ID]) if defined $incident->[INCIDENT_BUG_ID];

    if (defined $incident->[INCIDENT_DEBUGS]) {
        foreach my $debug (@{$incident->[INCIDENT_DEBUGS]}) {
            $writer->startTag('debug');
            $writer->dataElement('type', $debug->[INCIDENT_DEBUG_TYPE]);
            $writer->cdataElement('text', $debug->[INCIDENT_DEBUG_TEXT]);
            $writer->endTag('debug');
        }
    }
    
    #$writer->dataElement('host', $incident->[INCIDENT_HOST]) if defined $incident->[INCIDENT_HOST]; # hostname
    #$writer->dataElement('build_id', $incident->[INCIDENT_BUILD_ID]) if defined $incident->[INCIDENT_BUILD_ID]; # int
    #$writer->dataElement('binary', $incident->[INCIDENT_BINARY]) if defined $incident->[INCIDENT_BINARY]; # string
    #$writer->dataElement('role', $incident->[INCIDENT_ROLE]) if defined $incident->[INCIDENT_ROLE]; # string

    $writer->endTag('incident');

    $writer->end();

    return $incident_xml;
}

sub setId {
    $_[0]->[INCIDENT_ID] = $_[1];
}

1;
