# Copyright (C) 2009 Sun Microsystems, Inc. All rights reserved.
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

package GenTest::Executor::Dummy;

require Exporter;

@ISA = qw(GenTest::Executor);

use strict;
use DBI;
use GenTest;
use GenTest::Constants;
use GenTest::Result;
use GenTest::Executor;
use GenTest::Translator;
use GenTest::Translator::MysqlDML2ANSI;
use GenTest::Translator::Mysqldump2ANSI;
use GenTest::Translator::MysqlDML2javadb;
use GenTest::Translator::Mysqldump2javadb;
use GenTest::Translator::MysqlDML2pgsql;
use GenTest::Translator::Mysqldump2pgsql;


use Data::Dumper;


sub init {
	my $executor = shift;

    ## Just to have somthing that is not undefined
	$executor->setDbh($executor); 

    $executor->defaultSchema("schema");

	return STATUS_OK;
}

sub execute {
	my ($self, $query, $silent) = @_;

    $query = $self->preprocess($query);

    ## This may be generalized into a translator which is a pipe

    my @pipe;
    if ($self->dsn() =~ m/javadb/) {
        @pipe = (GenTest::Translator::Mysqldump2javadb->new(),
                 GenTest::Translator::MysqlDML2javadb->new());

    } elsif ($self->dsn() =~ m/postgres/) {

        @pipe = (GenTest::Translator::Mysqldump2pgsql->new(),
                 GenTest::Translator::MysqlDML2pgsql->new());

    }
    foreach my $p (@pipe) {
        $query = $p->translate($query);
        return GenTest::Result->new( 
            query => $query, 
            status => STATUS_WONT_HANDLE ) 
            if not $query;
        
    }

    if ($ENV{RQG_DEBUG} or $self->dsn() =~ m/print/) {
        print "Executing $query;\n";
    }

	return new GenTest::Result(query => $query,
                               status => STATUS_OK);
}


sub version {
	my ($self) = @_;
	return "Version N/A"; # Not implemented in DBD::JDBC
}

sub currentSchema {
    my ($self,$schema) = @_;
    return $self->defaultSchema();
}

sub getSchemaMetaData {
    return [['schema','tab','table','col1','ordinary'],
            ['schema','tab','table','col2','primary'],
            ['schema','tab','table','col3','indexed'],
            ['test','tab','table','col1','ordinary'],
            ['test','tab','table','col2','primary'],
            ['test','tab','table','col3','indexed'],
            ['mysql','tab','table','col','indexed'],
            ['performance_schema','tab','table','col','indexed'],
            ['INFORMATION_SCHEMA','tab','table','col','indexed']];
            
}

sub getCollationMetaData {
    return [['collation','charset']];
}

sub disconnect {
    my ($self) = @_;
    $self->setDbh(undef);
}

1;
