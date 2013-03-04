# Copyright (c) 2010,2011 Oracle and/or its affiliates. All rights
# reserved.
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

package GenTest::XML::Transporter;

require Exporter;
@ISA = qw(GenTest);

#@EXPORT = ('XMLTRANSPORT_TYPE_SCP', 'XMLTRANSPORT_TYPE_MYSQL');

use strict;
use GenTest;
use GenTest::Constants;
use GenTest::Properties;


use constant XMLTRANSPORT_TYPE              => 0;  # which transport type to use
use constant XMLTRANSPORT_TYPE_NONE         => 1;  # Noop. Does nothing
use constant XMLTRANSPORT_TYPE_MYSQL        => 2;  # db connections
use constant XMLTRANSPORT_TYPE_SCP          => 3;  # secure copy
use constant XMLTRANSPORT_TYPES             => 4;  # collection of types

# Defaults:
use constant XML_DEFAULT_TRANSPORT_TYPE     => XMLTRANSPORT_TYPE_SCP;
use constant XML_MYSQL_DEFAULT_DSN          => 
    'dbi:mysql:host=myhost:port=3306:user=xmldrop:password=test;database=test';
use constant XML_SCP_DEFAULT_USER           => undef;
use constant XML_SCP_DEFAULT_HOST           => 'regin.norway.sun.com';
use constant XML_SCP_DEFAULT_DEST_PATH      => '/raid/xml_results/TestTool/xml/';

1;  # so the require or use succeeds

#
# Use this class for transporting XML reports to a given destination.
#
# Usage example (using default settings):
#
#   use GenTest::XML::Transporter;
#   my $xml_transporter = GenTest::XML::Transporter->new(
#       type => undef)
#   );
#   my $result = $xml_transporter->sendXML($xmlFileName);
#   if ($result != STATUS_OK) {
#       croak("Error from XML Transporter: $result");
#   }
#
#
sub new {
	my $class = shift;

	my $self = $class->SUPER::new({
        type        => XMLTRANSPORT_TYPE
	}, @_);

    # Figure out transport type, which may be set as string value on
    # command-line, or elsewhere. Use default if not set.
    if (not defined $self->[XMLTRANSPORT_TYPE]) {
        $self->[XMLTRANSPORT_TYPE] = XML_DEFAULT_TRANSPORT_TYPE;
        say('XML Transport: Using default settings');
    } elsif ($self->[XMLTRANSPORT_TYPE] =~ m{scp}io) {
        # string match for "scp" (case insensitive)
        $self->[XMLTRANSPORT_TYPE] = XMLTRANSPORT_TYPE_SCP;
    } elsif ($self->[XMLTRANSPORT_TYPE] =~ m{mysql}io) {
        # string match for "mysql" (case insensitive)
        $self->[XMLTRANSPORT_TYPE] = XMLTRANSPORT_TYPE_MYSQL;
    } elsif ($self->[XMLTRANSPORT_TYPE] =~ m{none}io) {
        $self->[XMLTRANSPORT_TYPE] = XMLTRANSPORT_TYPE_NONE;
    }

    #${$self}[XMLTRANSPORT_TYPES] = ();

    return $self;
}


#
# Returns the type of transport mechanism this object represents.
#
sub type {
    my $self = shift;
    if (defined $self->[XMLTRANSPORT_TYPE]) {
        return $self->[XMLTRANSPORT_TYPE];
    } else {
        return XML_DEFAULT_TRANSPORT_TYPE;
    }
}

#
# Constructs a default destination for the SCP transport type.
# Suitable for use in an scp command-line such as:
#    scp myfile <defaultScpDestination>
# where <defaultScpDestination> is <user>@<host>:<path>.
#
sub defaultScpDestination {
    my $self = shift;
    my $dest = XML_SCP_DEFAULT_HOST.':'.XML_SCP_DEFAULT_DEST_PATH;
    $dest = XML_SCP_DEFAULT_USER.'@'.$dest if defined XML_SCP_DEFAULT_USER;
    return $dest;
}


#
# Sends XML data to a destination.
# The transport mechanism to use (e.g. file copy, database insert, ftp, etc.)
# and destination is determined by the "type" argument to the object's
# constructor.
#
# Arguments:
#   arg1: xml  - The xml data file name. TODO: Support XML as string?
#   arg2: dest - Destination for xml report. Defaults are used if omitted.
#
sub sendXML {
    my ($self, $xml, $dest) = @_;

    if ($self->type == XMLTRANSPORT_TYPE_NONE) {
        say("XML Transport type: NONE");
        return STATUS_OK;
    } elsif ($self->type == XMLTRANSPORT_TYPE_MYSQL) {
        say("XML Transport type: MySQL database connection");
        $dest = XML_MYSQL_DEFAULT_DSN if not defined $dest;
        return $self->mysql($xml, $dest);
    } elsif ($self->type == XMLTRANSPORT_TYPE_SCP) {
        say("XML Transport type: SCP");
        $dest = $self->defaultScpDestination if not defined $dest;
        return $self->scp($xml, $dest);
    } else {
        say("[ERROR] XML transport type '".$self->type."' not supported.");
        return STATUS_ENVIRONMENT_FAILURE;
    }


    
}

#
# Sends the XML contents of file $xml to $dest.
# If $dest is not defined, a default MySQL dsn will be used.
#
# TODO: - Support argument as string (real XML contents) instead of file name.
#       - Support non-default destination.
#
sub mysql() {
    my ($self, $xml, $dest) = @_;

    # TODO:
    # 1. Establish dbh / connect
    # 2. Execute query
    # 3. Check for errors
    # 4. Return appropriate status.
    say("MySQL XML transport not implemented yet");
    return STATUS_WONT_HANDLE;
}

#
# Sends the file $xml by SCP (secure file copy) to $dest.
#
sub scp {
    my ($self, $xml, $dest) = @_;

    # For now, we assume $xml is a file name
    # TODO: Support XML as string as well? Create temporary file?
    my $xmlfile = $xml;

    my $cmd;
    if (osWindows()) {
        # We currently support only pscp.exe from Putty on native Windows.
        #
        # NOTE: Using pscp without specifying private key (-i <keyfile>)
        #       requires that Putty's pageant tool is running and set up with
        #       the correct ssh key on the test host.
        #       If support for options is needed, add it below.
        $cmd = 'pscp.exe -q '.$xmlfile.' '.$dest;
    } else {
        $cmd = 'scp '.$xmlfile.' '.$dest;
    }

    say("SCP command is: ".$cmd);

    # TODO: The scp command is interactive if keys and hosts are not set up.
    #       This may cause hangs in automated environments. Find a way to
    #       always run non-interactively, or kill the command after a timeout.
    my $result = system($cmd);
    if ($result != STATUS_OK) {
        warn('XML Transport: scp failed. Command was: '.$cmd);
    }

    return $result >> 8;
}
