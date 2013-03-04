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

package GenTest::XML::Environment;

require Exporter;
@ISA = qw(GenTest);

use strict;
use Carp;
use Cwd;
use File::Spec;
use GenTest;
use Net::Domain qw(hostfqdn);

# Global variables keeping environment info
our $hostname = Net::Domain->hostfqdn();
our $arch;
our $kernel;
our $bit;
our $cpu;
our $memory;
#our $disk;
our $role = 'server';
our $locale;
our $encoding;
our $timezone;
our $osType;
our $osVer;
our $osRev;
our $osPatch;
our $osBit;
our $harnessVersion;
our $harnessRevision;


our $DEBUG=0;

sub new {
    my $class = shift;

    my $environment = $class->SUPER::new({
    }, @_);

    return $environment;
}

sub xml {
    require XML::Writer;

    # Obtain environmental info from host.
    # In separate function because lots of logic is needed to parse various
    # information based on the OS.
    getInfo();

    my $environment = shift;
    my $environment_xml;

    my $writer = XML::Writer->new(
        OUTPUT      => \$environment_xml,
        DATA_MODE   => 1,   # this and DATA_INDENT to have line breaks and indentation after each element
        DATA_INDENT => 2,   # number of spaces used for indentation
    );

    $writer->startTag('environments');
    $writer->startTag('environment', 'id' => 0);
    $writer->startTag('hosts');
    $writer->startTag('host');

    # Some elements may be empty either because
    #  a) we don't know that piece of information
    #  b) values are fetched from a database of test hosts
    $writer->dataElement('name', $hostname);
    $writer->dataElement('arch', $arch);
    $writer->dataElement('kernel', $kernel);
    $writer->dataElement('bit', $bit) if defined $bit;
    $writer->dataElement('cpu', $cpu);
    $writer->dataElement('memory', $memory);
    $writer->dataElement('disk', '');
    $writer->dataElement('role', $role);
    $writer->dataElement('locale', $locale);
    $writer->dataElement('encoding', $encoding);
    $writer->dataElement('timezone', $timezone);

    #
    # <software> ...
    #
    $writer->startTag('software');

    # Note that the order of the tags under software->program is significant. 
    # Some commented tags are part of XML spec but not implemented here yet.

    # <os>
    $writer->startTag('program');
    $writer->dataElement('name', $osType);
    $writer->dataElement('type', 'os');
    $writer->dataElement('version', $osVer);
    #$writer->dataElement('patch', $osPatch); # not in XML schema
    $writer->dataElement('bit', $osBit) if defined $osBit;
    $writer->endTag('program');

    # <program> perl
    $writer->startTag('program');
    $writer->dataElement('name', 'perl');
    $writer->dataElement('type', 'perl');
    #$writer->dataElement('version', $^V); # Solaris yields: Code point \u0005 is not a valid character in XML at lib/GenTest/XML/Environment.pm line 45
    $writer->dataElement('version', $]);
    $writer->dataElement('path', $^X);
    $writer->endTag('program');

    # <program> harness
    $writer->startTag('program');
    $writer->dataElement('name', 'Random Query Generator');
    $writer->dataElement('type', 'harness');
    $writer->dataElement('version', $harnessVersion) if defined $harnessVersion;
    $writer->dataElement('revision', $harnessRevision) if defined $harnessRevision;
    #<xsd:element name="patch" type="xsd:string" minOccurs="0" maxOccurs="1" form="qualified"/>
    my $rqg_path = File::Spec->rel2abs();   # assuming cwd is the randgen dir
    $writer->dataElement('path', $rqg_path);
    #<xsd:element name="bit" type="cassiopeia:Bits" minOccurs="0" form="qualified"/>
    #<xsd:element name="commandline_options" type="cassiopeia:Options" minOccurs="0" form="qualified"/>
    #<xsd:element name="commandline" minOccurs="0" type="xsd:string" form="qualified" /> # alternative to the above
    $writer->endTag('program');
    
    $writer->endTag('software');

    $writer->endTag('host');
    $writer->endTag('hosts');
    $writer->endTag('environment');
    $writer->endTag('environments');

    $writer->end();

    return $environment_xml;
}

sub getInfo()
{

    # First, OS-independent stuff:
    
    # Grab info from bzr, assuming current dir is randgen dir.
    # If not a bzr branch, information is undef.
    my $bzrinfo_randgen = GenTest::BzrInfo->new(
            dir => cwd()
    );
    # Using bzr revno as RQG version until we have something better.
    #
    # Update: Temporarily using revid as version (instead of revision) due to
    #         a "bug" in the XML schema where revision can only be an integer.
    #
    #         TODO: Use revid as "revision" and revno as "version" once XML 
    #               schema is fixed to accept "revision" tag as string (similar 
    #               to build->revision).
    #
    #         If the RQG starts to include a different version number, 
    #         independent of the underlying version control system, we may want 
    #         to use that as version and instead not report revno (revid is better to
    #         use as revision due to its ability to uniquely identify a revision
    #         over time).
    #
    #$harnessVersion = 'revno'.$bzrinfo_randgen->bzrRevno();
    #$harnessRevision = $bzrinfo_randgen->bzrRevisionId();
    $harnessVersion = $bzrinfo_randgen->bzrRevisionId();
    
    
    # lets see what OS type we have
    if (osLinux())
    {
        
        # Get the CPU info
        $cpu = trim(`cat /proc/cpuinfo | grep -i "model name" | head -n 1 | cut -b 14-`);
        my $numOfP = trim(`cat /proc/cpuinfo | grep processor |wc -l`);
        $cpu ="$numOfP"."x"."$cpu";

        #try to get OS Information
        if (-e "/etc/SuSE-release"){$osVer=`cat /etc/SuSE-release  |head -n 1`;}
        elsif (-e "/etc/redhat-release"){$osVer=`cat /etc/redhat-release  |head -n 1`;}
        elsif (-e "/etc/debian_version"){$osVer=`cat /etc/debian_version  |head -n 1`;}
        else {$osVer="linux-unknown";}
        $osVer=trim($osVer);
        if (-e "/etc/SuSE-release"){$osPatch=`cat /etc/SuSE-release  |tail -n 1`;}
        elsif (-e "/etc/redhat-release"){$osPatch=`cat /etc/redhat-release  |tail -n 1`;}
        elsif (-e "/etc/debian_version"){$osPatch=`cat /etc/debian_version  |tail -n 1`;}
        else {$osPatch="unknown";}
        (my $trash, $osPatch) = split(/=/,$osPatch);
        $osType="Linux";
        $arch=trim(`uname -m`);
        # We assume something like "x86_64" from 'uname -m'. Get bit info from that.
        ($trash, $bit) = split(/_/,$arch);
        # If nothing after '_' (or no '_' at all), assume 32-bit.
        $bit = "32" if length($bit) < 1;
        #$bit = undef if length($bit) < 1;
        $bit = trim($bit);
        $osBit = $bit;  # treat $osBit as $bit for now...

        $kernel=trim(`uname -r`);

        #Memory
        $memory = trim(`cat /proc/meminfo | grep -i memtotal`);
        $memory =~ s/MemTotal: //;
        ($memory, $trash) =  split(/k/,$memory);
        $memory = trim(`cat /proc/meminfo |grep -i memtotal`);
        $memory =~ /MemTotal:\s*(\d+)/;
        $memory = sprintf("%.2f",($1/1024000))."GB";

        #locale
        if (defined ($locale=`locale |grep LC_CTYPE| cut -b 10-`))
        {
            ($locale,$encoding) = split(/\./,$locale);
            $encoding = trim($encoding);
        }
        else
        {
            $locale   = "UNKNOWN";
            $encoding = "UNKNOWN";
        }

        #TimeZone
        $timezone = trim(`date +%Z`);
    }
    elsif(osSolaris())
    {
        
        # Get the CPU info
        my $tmpVar = `/usr/sbin/psrinfo -v | grep -i "operates" | head -1`;
        ($cpu, my $speed) = split(/processor operates at/,$tmpVar);
        $cpu =~ s/The//;
        $speed =~ s/MHz//;
        $cpu = trim($cpu);
        $speed = trim($speed);
        if ($speed => 1000)
        {
            $speed = $speed/1000;
            $speed = "$speed"."GHz";
        }
        else
        {
            $speed = "$speed"."MHz";
        }

        my $numOfP = `/usr/sbin/psrinfo -v | grep -i "operates" |wc -l`;
        $numOfP = trim($numOfP);
        $cpu ="$numOfP"."x"."$cpu"."$speed";

        #try to get OS Information
        ($osType,$osVer,$arch) = split (/ /, trim(`uname -srp`));
        # use of uname -m is discouraged (man pages), so use isainfo instead
        $kernel = trim(`isainfo -k`);
        $osBit = trim(`isainfo -b`);
        my $trash; # variable functioning as /dev/null
        ($trash, $trash, my $osPatch1, my $osPatch2, $trash) = split(/ /, trim(`cat /etc/release | head -1`));
        my $osPatch3 = `uname -v`;
        $osPatch = $osPatch1.' '.$osPatch2.' '.$osPatch3;
        $osPatch = trim($osPatch);

        #Memory
        $memory = `/usr/sbin/prtconf | grep Memory`;
        $memory =~ s/Memory size://;
        $memory =~ s/Megabytes//;
        $memory = trim($memory);
        $memory = $memory/1024;
        ($memory, my $trash) = split(/\./,$memory);
        $memory = "$memory"."GB";

        #locale
        if (defined ($locale=`locale |grep LC_CTYPE| cut -b 10-`))
        {
            ($locale, $encoding) = split(/\./,$locale);
            $encoding = trim($encoding);
        }
        else
        {
            $locale   = "UNKNOWN";
            $encoding = "UNKNOWN";
        }

        #TimeZone
        $timezone = trim(`date +%Z`);
    }
    elsif(osWindows())
    {
        #$hostName = `hostname`;
        my @tmpData;
        if ($^O eq 'cygwin')
        {
            # Assuming cygwin on Windows at this point
            @tmpData = `cmd /c systeminfo 2>&1`;
        }
        else
        {
            # Assuming Windows at this point
            @tmpData = `systeminfo 2>&1`;
        }

        if ($? != 0)
        {
            carp "systeminfo command failed with: $?";
            $cpu        = "UNKNOWN";
            $osType     = "UNKNOWN";
            $osVer      = "UNKNOWN";
            $arch       = "UNKNOWN";
            $kernel     = "UNKNOWN";
            $memory     = "UNKNOWN";
            $locale     = "UNKNOWN";
            $timezone   = "UNKNOWN";
        }
        else
        {
            $kernel = "UNKOWN";
            my $cpuFlag = 0;
            # Time to get busy and grab what we need.
            foreach my $line (@tmpData)
            {
                if ($cpuFlag == 1)
                {
                    (my $numP, $cpu) = split(/\:/,$line);
                    $numP = trim($numP);
                    (my $trash, $numP) = split(/\[/,$numP);
                    ($numP, $trash) = split(/\]/,$numP);
                    $cpu = "$numP"."$cpu";
                    $cpu = trim($cpu);
                    $cpuFlag=0;
                }
                elsif ($line =~ /OS Name:\s+(.*?)\s*$/)
                {
                    $osType = $1;
                }
                elsif ($line =~ /^OS Version:\s+(.*?)\s*$/)
                {
                    $osVer = $1;
                }
                elsif ($line =~ /System type:\s/i)
                {
                    (my $trash, $arch) = split(/\:/,$line);
                    ($arch,$trash) = split(/\-/,$arch);
                    $arch = trim($arch);
                }
                elsif ($line =~ /^Processor/)
                {
                    $cpuFlag = 1;
                    next;
                }
                elsif ($line =~ /^Total Physical Memory:\s+(.*?)\s*$/)
                {
                    $memory = $1;
                }
                elsif ($line =~ /Locale:/)
                {
                    (my $trash, $locale) = split(/\:/,$line);
                    ($locale, $trash) = split(/\;/,$locale);
                    $locale = trim($locale);
                }
                elsif ($line =~ /Time Zone:\s+(.*?)\s*$/)
                {
                    $timezone = $1;
                }
            }
        }
    }
    else
    {
        confess "\n\nUnable to figure out OS!!\n\n";
    }

    if ($DEBUG)
    {
        print "cpu      = $cpu\n";
        print "os       =  $osType\n";
        print "OS ver   = $osVer\n";
        print "Arch     = $arch\n";
        print "Kernel   = $kernel\n";
        print "memory   = $memory\n";
        print "locale   = $locale\n";
        print "Timezone = $timezone\n";
    }
}

# Removes leading and trailing whitespace and trailing newline characters
sub trim($)
{
    my $string = shift;
    $string =~ s/^\s+//;
    $string =~ s/\s+$//;
    chomp($string);
    return $string;
}

1;
