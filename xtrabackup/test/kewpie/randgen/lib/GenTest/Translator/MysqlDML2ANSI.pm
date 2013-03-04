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

## Translator that translates some frequently used MySQL DML
## constructs to ANSI


package GenTest::Translator::MysqlDML2ANSI;

@ISA = qw(GenTest::Translator GenTest);

use strict;

use GenTest;

sub limit {
    my $dml = $_[1];
    $dml =~ s/\bLIMIT\s+(\d+)\s+OFFSET\s+(\d+)/OFFSET \2 ROWS FETCH NEXT \1 ROWS ONLY/;
    $dml =~ s/\bLIMIT\s+(\d+)\s*,\s*(\d+)/OFFSET \1 ROWS FETCH NEXT \2 ROWS ONLY/;
    $dml =~ s/\bLIMIT\s+(\d+)/FETCH FIRST \1 ROWS ONLY/gi;
    return $dml;
}

sub supported_join() {
    return 1;
}

sub join() {
    my $self = $_[0];
    my @p;
    $p[0]=$_[1];

    ## The subsitution in the while loop won't terminate if the number
    ## of parenthesises don't match, So we count them first:
    
    my $left = ($p[0] =~ tr/\(//);
    my $right = ($p[0] =~ tr/\)//);

    if ($left != $right) {
        ## Drop JOIN tranlation if parenthesises don't match
        return $p[0];
    }

    my $paren_rx;
    
    $paren_rx = qr{
  (?:
    \((??{$paren_rx})\) # either match another paren-set
    | [^()]+            # or match non-parens (or escaped parens
  )*
}x;
    
    my $n=0;
    my $m=-1;
    while ($m < $n)
    {
        $m++;
        my $str=$p[$m];
#        $str =~ s{(\(\s*SELECT\s+(??{$paren_rx})\))}{
        $str =~ s{(\(\s*(??{$paren_rx})\))}{
            my $hit = $1;
            $hit =~ s/^\((.*)\)$/\1/s;
            $n++;
            $p[$n] = $hit;
            " xxxx".$n."xxxx "
        }sgexi;
        $p[$m]=$str;
    };

    for (my $i=0; $i<=$n; $i++) {
        if (not $self->supported_join($p[$i])) {
            return 0;
        }
        my @c = split(/(\bCROSS\s+JOIN\b|\bINNER\s+JOIN\b|\bSTRAIGHT_JOIN\b|\bRIGHT\s+JOIN\b|\bLEFT\s+JOIN\b|\bFULL\s+JOIN\b|\bFULL\s+OUTER\s+JOIN\b|\bRIGHT\s+OUTER\s+JOIN\b|\bLEFT\s+OUTER\s+JOIN\b|\bJOIN\b|;)/,$p[$i]);
        for (my $j=0; $j<$#c; $j++) {
            if ($c[$j] =~ m/(RIGHT|FULL|LEFT|OUTER)\s+JOIN/i) {
                ### Do not fix OUTER JOINS. They're presumably ok
            } elsif ($c[$j] =~ m/\bSTRAIGHT_JOIN\b/) {
                ## 1. Fix STRAIGHT_JOIN
                if ($c[$j+1] =~ m/\bON\b/) {
                    $c[$j] =~ s/\bSTRAIGHT_JOIN\b/INNER JOIN/i;
                } else {
                    $c[$j] =~ s/\bSTRAIGHT_JOIN\b/CROSS JOIN/i;
                }
            } elsif ($c[$j] =~ m/\bCROSS\s+JOIN\b/) {
                ## 1. Fix CROSS JOIN with ON clause
                if ($c[$j+1] =~ m/\bON\b/) {
                    $c[$j] =~ s/\bCROSS\s+JOIN\b/INNER JOIN/i;
                }
            } elsif ($c[$j] =~ m/\bINNER\s+JOIN\b/) {
                ## 1. Fix INNER JOIN without ON clause
                if (not $c[$j+1] =~ m/\bON\b/) {
                    $c[$j] =~ s/\bINNER\s+JOIN\b/CROSS JOIN/i;
                }
            } elsif ($c[$j] =~ m/\bJOIN\b/) {
                ## Fix JOIN without ON clause
                if (not $c[$j+1] =~ m/\bON\b/) {
                    $c[$j] =~ s/\bJOIN\b/CROSS JOIN/i;
                }
            }
            ### Fix ON expression to ON (expression <> 0). This is a
            ### bad hack, but will work in a lot of the cases
            #if ($c[$j] =~ m/\bJOIN\b/i) {
            #    $c[$j+1] =~ s/\bON\s*([a-z0-9_`"]+\s*\.\s*[a-z0-9_\`"]+)/ON (\1 <> 0)/gi;
            #}

        }
        $p[$i] = join("",@c);
    }

    ## Final stuff
    for (my $i=0; $i<=$n; $i++) {
        ## Change all CROSS JOIN to ,-syntax
        $p[$i] =~ s/\bCROSS JOIN\b/,/gi;
    }
    
    for (my $i=$n; $i>=0; $i--) {
        while ($p[$i] =~ m/ xxxx(\d+)xxxx /) {
            my $no=$1;
            my $pattern = " xxxx".$no."xxxx ";
            $p[$i] =~ s/$pattern/\($p[$no]\)/;
        }
    }
    
    return $p[0];
}


my $lineno;
my @result;
sub translate {

    my $self = $_[0];

    my $dml = $_[1];

    # print ">>>>>>>>>>>>>>>>>>", $dml;
    
    $dml =~ s/\bSQL_SMALL_RESULT\b//gsi;

    ## SELECT STRAIGHT_JOIN is just translated to SELECT
    $dml =~ s/\bSELECT\s*STRAIGHT_JOIN\b/SELECT/gsi;

    $dml =~ s/CONCAT\s*\(([^,]+),([^)]+)\)/\(\1 || \2 \)/gsi;
    
    ## Translate LIMIT semantics into ANSI
    $dml = $self->limit($dml);

    ## Translate JOINS
    $dml = $self->join($dml);

    # print "<<<<<<<<<<<<<<<<<<", $dml;

    return $dml;
}

1;
