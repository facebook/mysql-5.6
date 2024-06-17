# Copyright (c) 2011, 2012 Oracle and/or its affiliates. All rights reserved.
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

# Do a simple run of scripts to see that they're sound
#
package TestTT;
use base qw(Test::Unit::TestCase);
use lib 'lib';
use GenTest;
use Cwd;
use Net::Ping;
use LWP::Simple;

sub new {
    my $self = shift()->SUPER::new(@_);
    # your state for fixture here
    return $self;
}

my $generator;  
my $counter=0; # to avoid port clashes.

sub set_up {
    my $self=shift;
    # Set temporary working directory. Used for vardir, workdir etc. in tests. 
    # Remove it in tear_down() to avoid interference between tests!
    $self->{workdir} = cwd()."/unit/tmpwd2"; 
    my $portbase = ($counter*10) + ($ENV{TEST_PORTBASE}>0 ? int($ENV{TEST_PORTBASE}) : 22120);
    $self->{portbase} = int(($portbase - 10000) / 10);
    $counter++;
}

sub tear_down {
    # clean up after test
    unlink "unit/tmp/test1.xml";
    unlink "unit/tmp/test2.xml";
    unlink "unit/tmp/foo1.log";
    unlink "unit/tmp/foo2.log";
    unlink "unit/tmp/testresult-schema-1-2.xsd";
    system("rm -r unit/tmp/example*");
    
    # Not all tests use the workdir, so we need to check if it exists.
    if (-e $self->{workdir}) {
        rmtree($self->{workdir}) or print("UNABLE TO REMOVE DIR ".$self->{workdir}.": $!\n");
    }
}


# Check the schemalcoation URI's
# and gather the xsd location for validating.
sub validate_schema {
    my ($self,$file)=@_;
    
    # If the Module XML::Path installed then
    # use it for parsing the xml file.
    my $nodes;
    eval
    {
        require XML::XPath;
        $nodes = XML::XPath->new($file);
    };
    
    if ($nodes) {
        my $nodeSet = $nodes->find("/");
        $self->assert_not_null($nodeSet);
        
        # Find the report tag in the xml file.
        my $rootNode = $nodeSet->get_node("/report");
        $self->assert_not_null($rootNode, "Unable to find the report node");
        
        my $rootNodeString = $rootNode->toString();
        $self->assert_not_null($rootNodeString);
        
        # Find the URI's with the schemalocation attribute.
        my $schemaLocations;
        my %schemaLocations;
        if ($rootNodeString =~ /schemaLocation\s*=\s*"(.*?)"/i) { $schemaLocations = $1; }
        $self->assert_not_null($schemaLocations,"Could not find schemaLocation in file $file");
        
        while ($schemaLocations =~ /\s*([^\s]+?)\s+([^\s]+)\s*/g) { $schemaLocations{$1} = 1; $schemaLocations{$2} = 1; }
        
        # Verify whether the two URI's are present.
        my $num_schemata=0;  
        my $xsd;
        foreach my $uri (keys%schemaLocations) {
            if ( exists $schemaLocations{$uri} ) {
                $xsd=$uri if ( $uri =~ /xsd$/i );
                $num_schemata++;
            }
        }
        
        # We are expecing 2 URI's.
        $self->assert_equals(2,$num_schemata,"Schemalcoation does not contain two URI's");
        
        $self->assert_not_null($xsd,"Unable to get xsd URI in schemalocation attribute");
        
        # Return the xsd file location.
        return $xsd;
    }
   
}

# Vlaidate the xml file with the xsd schema.
sub validate_xml {
    my ($self, $file)=@_;
    my $status;
    
    # If the Module XML::LibXML is installed then
    # use it for validating the xml file.
    my $parser;
    eval
    {
        require XML::LibXML;
        $parser = XML::LibXML->new;
    };
    
    if ($parser) {
        my $host="regin.no.oracle.com";
        
        # Check if we can reach the host.
        my $ping = Net::Ping->new();
        $self->assert_not_null($ping);
        
        if ($ping->ping($host)) {
            # Retrieve and validate the xsd file path from the xml.
            my $xsd=$self->validate_schema($file);
            
            # Grab the page and store the content of the xsd.
            my $content = get $xsd;
            $self->assert_not_null($content,"Unable to grab the content from $xsd location");
            $ping->close();
           
            # Write xsd to temporatry file.
            open (FH, ">unit/tmp/testresult-schema-1-2.xsd") or die $!;
            print FH $content;
            close (FH);
                        
            # Load the schena xsd file.
            my $schema = XML::LibXML::Schema->new(location => "unit/tmp/testresult-schema-1-2.xsd");
            $self->assert_not_null($schema);
            
            # Validate the xml file with the xsd.
        my $doc = $parser->parse_file($file);
            $self->assert_not_null($doc);

            eval { $status=$schema->validate($doc) };
            say("$file validated successfully against schema $xsd") if not $@;
            $self->assert_not_null($status,"XML Validation failed of $file with $xsd");
        }else{
            say("Unable to reach $host, will not be able to validate $file");
        }
    }
}

sub test_xml_runall {
    if ($ENV{TEST_SKIP_RUNALL}) {
        say((caller(0))[3].": Skipping runall.pl test");
        return;
    }
    my $self = shift;
    my $pb = $self->{portbase};
    my $file = "unit/tmp/test1.xml";
    ## This test requires RQG_MYSQL_BASE to point to a in source Mysql database
    if ($ENV{RQG_MYSQL_BASE}) {
        $ENV{LD_LIBRARY_PATH}=join(":",map{"$ENV{RQG_MYSQL_BASE}".$_}("/libmysql/.libs","/libmysql","/lib/mysql"));
        my $status = system("perl -MCarp=verbose ./runall.pl --mtr-build-thread=$pb --grammar=conf/examples/example.yy --gendata=conf/examples/example.zz --queries=3 --threads=3 --report-xml-tt --report-xml-tt-type=none  --xml-output=$file --logfile=unit/tmp/foo1.log --report-tt-logdir=unit/tmp --basedir=".$ENV{RQG_MYSQL_BASE}." --vardir=".$self->{workdir});
        $self->assert_equals(0, $status);
        
        # Check whether the logfile gets created
        $self->assert(-e 'unit/tmp/foo1.log',"RQG log file does not exist.");
        
        # Validate the xml file.    
        $self->validate_xml($file);
    }
}

sub test_xml_runall_new {
    my $self = shift;
    my $file = "unit/tmp/test2.xml";
    ## This test requires RQG_MYSQL_BASE to point to a Mysql database (in source, out of source or installed)
    my $pb = $self->{portbase};
    
    
    if ($ENV{RQG_MYSQL_BASE}) {
        $ENV{LD_LIBRARY_PATH}=join(":",map{"$ENV{RQG_MYSQL_BASE}".$_}("/libmysql/.libs","/libmysql","/lib/mysql"));
        my $status = system("perl -MCarp=verbose ./runall-new.pl --mtr-build-thread=$pb --grammar=conf/examples/example.yy --gendata=conf/examples/example.zz --queries=3 --threads=3 --report-xml-tt --report-xml-tt-type=none --xml-output=$file --logfile=unit/tmp/foo2.log --report-tt-logdir=unit/tmp --basedir=".$ENV{RQG_MYSQL_BASE}." --vardir=".$self->{workdir});
        $self->assert_equals(0, $status);
        
        # Check whether the logfile gets created
        $self->assert(-e 'unit/tmp/foo2.log',"RQG log file does not exist.");
        
        # Validate the xml file.
        $self->validate_xml($file);
    }
}

1;
