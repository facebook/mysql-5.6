# Copyright (c) 2008, 2010 Oracle and/or its affiliates. All rights reserved.
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

package GenTest::XML::BuildInfo;

require Exporter;
@ISA = qw(GenTest);

use strict;
use GenTest;
use GenTest::BzrInfo;
use DBI;

use constant BUILDINFO_DSNS     => 0;
use constant BUILDINFO_SERVERS  => 1;

use constant BUILDINFO_SERVER_VERSION   => 0;
use constant BUILDINFO_SERVER_PACKAGE   => 1;
use constant BUILDINFO_SERVER_BIT       => 2;
use constant BUILDINFO_SERVER_PATH      => 3;
use constant BUILDINFO_SERVER_VARIABLES => 4;
use constant BUILDINFO_SERVER_TREE      => 5;
use constant BUILDINFO_SERVER_REVISION  => 6;

sub new {
    my $class = shift;

    my $buildinfo = $class->SUPER::new({
        dsns    => BUILDINFO_DSNS
    }, @_);

    $buildinfo->[BUILDINFO_SERVERS] = [];

    foreach my $id (0..$#{$buildinfo->[BUILDINFO_DSNS]})
    {
        my $dsn = $buildinfo->[BUILDINFO_DSNS]->[$id];
        next if $dsn eq '';
        my $dbh = DBI->connect($dsn);

        my $server;

        # TODO: Add support for non-MySQL dsns.
        
        # set this first because it may be used below
        $server->[BUILDINFO_SERVER_PATH] = $dbh->selectrow_array('SELECT @@basedir');
        
        # Server Version: 
        #   To align with other test drivers reporting to the same place we 
        #   report only the three x.y.z version numbers as "version".
        #   Expecting e.g "5.5.4-m3-log-debug", using only "5.5.4".
        #   The rest (e.g. "log-debug" as well as the version will be prepended
        #   to the value of the "package" tag instead.
        my $long_version = $dbh->selectrow_array('SELECT @@version');
        if ($long_version =~ /^(\d+\.\d+\.\d+)/ )
        {
            # grab version number only to use for version tag.
            $server->[BUILDINFO_SERVER_VERSION] = $1;
        } else {
            # version not of the expected format, so use the whole thing.
            $server->[BUILDINFO_SERVER_VERSION] = $long_version;
        }
        
        $server->[BUILDINFO_SERVER_PACKAGE] = $long_version . ' ' .
            $dbh->selectrow_array('SELECT @@version_comment');

        # Source code version-control information (tree, revision):
        #   First we check some environment variables for information: 
        #     BRANCH_SOURCE - source URL as understood by the underlying version
        #                     control system.
        #     BRANCH_NAME   - possibly non-unique name of the branch.
        #     PUSH_REVISION - unique ID for the current revision of the branch, 
        #                     as provided by the underlying version control system.
        #
        # If those are not present, we try to query bzr itself.

        # This may take a few seconds due to bzr slowness.
        my $bzrinfo_server = GenTest::BzrInfo->new(
            dir => $server->[BUILDINFO_SERVER_PATH]
        );
        
        # revision
        if (defined $ENV{'PUSH_REVISION'}) {
            $server->[BUILDINFO_SERVER_REVISION] = $ENV{'PUSH_REVISION'};
        } else {
            # If this fails, it remains undefined and will not be reported.
            $server->[BUILDINFO_SERVER_REVISION] = $bzrinfo_server->bzrRevisionId();
        }
        
        # tree:
        #   Since BRANCH_NAME may not be uniquely identifying a branch, we
        #   instead use the final part of the source URL as tree (branch) name.
        #   E.g. in PB2, we may have both daily-trunk and mysql-trunk referring
        #   to the same branch.
        if (defined $ENV{'BRANCH_SOURCE'}) {
            # extract last part of source URL and use as tree name.
            # Example: 
            #   http://host.com/bzr/server/mysql-next-mr
            #   becomes "mysql-next-mr".
            # Need to account for possible ending '/'.
            if ($ENV{BRANCH_SOURCE} =~ m{([^\\\/]+)[\\\/]?$}) {
                $server->[BUILDINFO_SERVER_TREE] = $1;
            }
        } else {
            if (defined $ENV{'BRANCH_NAME'}) {
                $server->[BUILDINFO_SERVER_TREE] = $ENV{'BRANCH_NAME'};
            } else {
                # Get branch nick from bzr
                # If this fails, it remains undefined and will not be reported.
                $server->[BUILDINFO_SERVER_TREE] = $bzrinfo_server->bzrBranchNick();
            }
        }
        
        # According to the schema, bit must be "32" or "64".
        # There is no reliable way to get this on all supported platforms using MySQL queries.
        #$server->[BUILDINFO_SERVER_BIT] = $dbh->selectrow_array('SELECT @@version_compile_machine');
        
        $server->[BUILDINFO_SERVER_VARIABLES] = [];
        my $sth = $dbh->prepare("SHOW VARIABLES");
        $sth->execute();
        while (my ($name, $value) = $sth->fetchrow_array()) {
            push @{$server->[BUILDINFO_SERVER_VARIABLES]}, [ $name , $value ];
        }
        $sth->finish();

        $dbh->disconnect();

        $buildinfo->[BUILDINFO_SERVERS]->[$id] = $server;
    }

    return $buildinfo;
}

sub xml {
    require XML::Writer;

    my $buildinfo = shift;
    my $buildinfo_xml;

    my $writer = XML::Writer->new(
        OUTPUT      => \$buildinfo_xml,
        DATA_MODE   => 1,   # this and DATA_INDENT to have line breaks and indentation after each element
        DATA_INDENT => 2,   # number of spaces used for indentation
    );

    $writer->startTag('product');
    $writer->dataElement('name','MySQL');
    $writer->startTag('builds');

    foreach my $id (0..$#{$buildinfo->[BUILDINFO_DSNS]})
    {
        my $server = $buildinfo->[BUILDINFO_SERVERS]->[$id];
        next if not defined $server;

        # Note that the order of these tags (sequence) is significant.
        # Commented tags are part of XML spec but not implemented here yet.

        $writer->startTag('build', id => $id);
        $writer->dataElement('version', $server->[BUILDINFO_SERVER_VERSION]);
        $writer->dataElement('tree', $server->[BUILDINFO_SERVER_TREE]) if defined $server->[BUILDINFO_SERVER_TREE];
        $writer->dataElement('revision', $server->[BUILDINFO_SERVER_REVISION]) if defined $server->[BUILDINFO_SERVER_REVISION];
        #<xsd:element name="tag" type="xsd:string" minOccurs="0" form="qualified"/>
        $writer->dataElement('package', $server->[BUILDINFO_SERVER_PACKAGE]);
        #$writer->dataElement('bit', $server->[BUILDINFO_SERVER_BIT]); # Must be 32 or 64
        $writer->dataElement('path', $server->[BUILDINFO_SERVER_PATH]);
        #<xsd:element name="compile_options" type="cassiopeia:Options" minOccurs="0" form="qualified"/>
        #<xsd:element name="commandline" type="xsd:string" minOccurs="0" form="qualified" />
        #<xsd:element name="buildscript" minOccurs="0" type="xsd:string" form="qualified" />
        $writer->endTag('build');
    }


    $writer->endTag('builds');

    $writer->startTag('binaries'); # --> <software> = <program>

    foreach my $id (0..$#{$buildinfo->[BUILDINFO_DSNS]})
    {
        my $server = $buildinfo->[BUILDINFO_SERVERS]->[$id];
        next if not defined $server;

        $writer->startTag('program');
        $writer->dataElement('name', 'mysqld');
        $writer->dataElement('type', 'database');
        $writer->startTag('commandline_options');

    # TODO: List actual commmand-line options (and config file options /
    #       RQG-defaults?), not all server variables?
        foreach my $option (@{$server->[BUILDINFO_SERVER_VARIABLES]})
        {
            $writer->startTag('option');
            $writer->dataElement('name', $option->[0]);
            $writer->dataElement('value', $option->[1]);
            $writer->endTag('option');
        }

        $writer->endTag('commandline_options');
        $writer->endTag('program');
    }

    $writer->endTag('binaries');
    $writer->endTag('product');
    $writer->end();

    return $buildinfo_xml;
}

1;
