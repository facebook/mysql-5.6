# Copyright (c) 2010 Sun Microsystems, Inc. All rights reserved.
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

package GenTest::BzrInfo;

@ISA = qw(GenTest);

use strict;
use Carp;
use GenTest;

# Helper variables
use constant BZRINFO_DIR            => 0;   # directory to check for bzr info

# Data from bzr itself
use constant BZRINFO_DATE           => 1;   # date of last revision
use constant BZRINFO_BUILD_DATE     => 2;   # date of last update/change to working copy
use constant BZRINFO_REVNO          => 3;   # revision number
use constant BZRINFO_REVISION_ID    => 4;   # revision id
use constant BZRINFO_BRANCH_NICK    => 5;   # branch nickname
use constant BZRINFO_CLEAN          => 6;   # 0 if the tree contains uncommitted changes, 1 otherwise


#
# Use this utility class for obtaining information about a file tree that is 
# version-controlled by Bazaar (bzr).
#
# Usage example:
#
#   use GenTest::BzrInfo;
#   my $bzrinfo_randgen = GenTest::BzrInfo->new(
#       dir => cwd()
#   );
#   print "RQG's bzr revision id is $bzrinfo_randgen->bzrRevisionId() \n";
#
sub new {
    my $class = shift;

    my $self = $class->SUPER::new({
        dir        => BZRINFO_DIR
    }, @_);

    # Read from bzr:
    $self->_getVersionInfo();

    return $self;
}


#getVersionInfo:
#  Read information from bzr and set object variables accordingly.
#  Run this when instantiating a new object (in new()).
#
#  Note: There is no well-defined place to get branch name. 'bzr info'
#        may provide "submit branch", "parent branch", "push branch", 
#        but this is not guaranteed.
#        There is also "branch nick" from 'bzr version-info'...
sub _getVersionInfo {
    my $self = shift;

    # Read bzr information and store relevant parts:

    # "bzr version-info" command allows customized output (see "bzr help 
    # version-info"). We use this to get information in the form of values of 
    # perl variables stored in a file, which we then read back in, and thus 
    # avoid extra parsing of the output.
    my ($date, $build_date, $clean, $revno, $revid, $nick); # align with contents of template below
    my $bzr_output_file = "rqg-bzr-version-info".$$.".txt";
    $bzr_output_file = tmpdir().$bzr_output_file if defined tmpdir();
    # Assigning values to Perl variables in a file from a system() command
    # requires proper escaping of special characters that may be interpreted by
    # the shell. On *nix we need to escape '$', on Windows we don't.
    my $var;
    if (osWindows()) {
        $var = '$';
    } else {
        $var = '\$';
    }
    # We grab all info provided by "bzr version-info", it takes no longer than
    # to get only what is currently needed.
    my $cmd = 'bzr version-info  --custom --template="'.
        $var.'date=\'{date}\';\n'.
        $var.'build_date=\'{build_date}\';\n'.
        $var.'clean=\'{clean}\';\n'.
        $var.'revno=\'{revno}\';\n'.
        $var.'revid=\'{revision_id}\';\n'.
        $var.'nick=\'{branch_nick}\';\n"';
    $cmd = $cmd.' '.$self->[BZRINFO_DIR] if defined $self->[BZRINFO_DIR];

    # Run the command and redirect output to the specified file.
    # Note: This may take a few seconds (due to bzr slowness)...
    system($cmd . " > $bzr_output_file 2>&1");
    if ($? != 0) {
        # Command failed. Common causes:
        #  - bzr binary is not available in PATH
        #  - the directory is not a bazaar repository
        # We keep values as undef to indicate that.
        say("BzrInfo: Unable to get version-information from bzr at ".
            $self->[BZRINFO_DIR].'. Not a bzr branch?') if rqg_debug();
        return;
    } else {
        open(BZROUT, $bzr_output_file) or
            say("BzrInfo: Unable to open temporary file ".$bzr_output_file.": $!");
        # Run eval on each line in order to set the variables.
        # Note: If eval fails (command returned OK but did not do as expected)
        #       perl will die.
        while (<BZROUT>) {
            eval;
        }
        $self->[BZRINFO_DATE] = $date;
        $self->[BZRINFO_BUILD_DATE] = $build_date;
        $self->[BZRINFO_REVNO] = $revno;
        $self->[BZRINFO_REVISION_ID] = $revid;
        $self->[BZRINFO_BRANCH_NICK] = $nick;
        $self->[BZRINFO_CLEAN] = $clean;
        close(BZROUT) or say("BzrInfo: Unable to close temporary file: $!");
    }

    # clean up
    unlink $bzr_output_file or
        say("BzrInfo: Unable to delete tmp file ".$bzr_output_file." $!");

    # Alternatively, run 'bzr info' as well and get relevant info.
    #if (open(BZRINFO, 'bzr info |')) {
    #    while (<BZRINFO>) {
    #        if (...matching_some_regex...) {
    #            # do something
    #        }
    #    }
    #} else {
    #    # non-zero exit code. Could be that bzr is not in path or something
    #    # else went wrong...
    #    say("Unable to get info from bzr: $!") if rqg_debug();
    #}

}

################################################################################
#   "Public" information below...
#   All may return undef if bzr itself or the information sought was not found.
###

sub bzrDate {
    my $self = shift;
    return $self->[BZRINFO_DATE];
}

sub bzrBuildDate {
    my $self = shift;
    return $self->[BZRINFO_BUILD_DATE];
}

sub bzrRevno {
    my $self = shift;
    return $self->[BZRINFO_REVNO];
}

sub bzrRevisionId {
    my $self = shift;
    return $self->[BZRINFO_REVISION_ID];
}

sub bzrBranchNick {
    my $self = shift;
    return $self->[BZRINFO_BRANCH_NICK];
}

sub bzrClean {
    my $self = shift;
    return $self->[BZRINFO_CLEAN];
}


1; # so the require or use succeeds
