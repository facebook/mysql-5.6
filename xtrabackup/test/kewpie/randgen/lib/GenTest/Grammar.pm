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

package GenTest::Grammar;

require Exporter;
@ISA = qw(GenTest);
@EXPORT = qw(
	GRAMMAR_FLAG_COMPACT_RULES
	GRAMMAR_FLAG_SKIP_RECURSIVE_RULES
);

use strict;

use GenTest;
use GenTest::Constants;
use GenTest::Grammar::Rule;
use GenTest::Random;

use Data::Dumper;

use constant GRAMMAR_RULES	=> 0;
use constant GRAMMAR_FILE	=> 1;
use constant GRAMMAR_STRING	=> 2;
use constant GRAMMAR_FLAGS	=> 3;

use constant GRAMMAR_FLAG_COMPACT_RULES		=> 1;
use constant GRAMMAR_FLAG_SKIP_RECURSIVE_RULES	=> 2;

1;

sub new {
	my $class = shift;


	my $grammar = $class->SUPER::new({
		'grammar_file'			=> GRAMMAR_FILE,
		'grammar_string'		=> GRAMMAR_STRING,
		'grammar_flags'		=> GRAMMAR_FLAGS,
		'grammar_rules'		=> GRAMMAR_RULES
	}, @_);


    if (defined $grammar->rules()) {
        $grammar->[GRAMMAR_STRING] = $grammar->toString();
    } else {
        $grammar->[GRAMMAR_RULES] = {};
        
        if (defined $grammar->file()) {
            my $parse_result = $grammar->parseFromFile($grammar->file());
            return undef if $parse_result > STATUS_OK;
        }
        
        if (defined $grammar->string()) {
            my $parse_result = $grammar->parseFromString($grammar->string());
            return undef if $parse_result > STATUS_OK;
        }
    }

	return $grammar;
}

sub file {
	return $_[0]->[GRAMMAR_FILE];
}

sub string {
	return $_[0]->[GRAMMAR_STRING];
}


sub toString {
	my $grammar = shift;
	my $rules = $grammar->rules();
	return join("\n\n", map { $grammar->rule($_)->toString() } sort keys %$rules);
}


sub parseFromFile {
	my ($grammar, $grammar_file) = @_;

	open (GF, $grammar_file) or die "Unable to open() grammar $grammar_file: $!";
	read (GF, my $grammar_string, -s $grammar_file) or die "Unable to read() $grammar_file: $!";

	$grammar->[GRAMMAR_STRING] = $grammar_string;

	return $grammar->parseFromString($grammar_string);
}

sub parseFromString {
	my ($grammar, $grammar_string) = @_;

	#
	# provide an #include directive 
	#

	while ($grammar_string =~ s{#include [<"](.*?)[>"]$}{
		{
			my $include_string;
			my $include_file = $1;
		        open (IF, $1) or die "Unable to open include file $include_file: $!";
		        read (IF, my $include_string, -s $include_file) or die "Unable to open $include_file: $!";
			$include_string;
	}}mie) {};

	# Strip comments. Note that this is not Perl-code safe, since perl fragments 
	# can contain both comments with # and the $# expression. A proper lexer will fix this
	
	$grammar_string =~ s{#.*$}{}iomg;

	# Join lines ending in \

	$grammar_string =~ s{\\$}{ }iomg;

	# Strip end-line whitespace

	$grammar_string =~ s{\s+$}{}iomg;

	# Add terminating \n to ease parsing

	$grammar_string = $grammar_string."\n";

	my @rule_strings = split (";\s*[\r\n]+", $grammar_string);

	my %rules;

	foreach my $rule_string (@rule_strings) {
		my ($rule_name, $components_string) = $rule_string =~ m{^(.*?)\s*:(.*)$}sio;

		$rule_name =~ s{[\r\n]}{}gsio;
		$rule_name =~ s{^\s*}{}gsio;

		next if $rule_name eq '';

		say("Warning: Rule $rule_name is defined twice.") if exists $rules{$rule_name};

		my @component_strings = split (m{\|}, $components_string);
		my @components;
		my %components;

		foreach my $component_string (@component_strings) {
			# Remove leading whitespace
			$component_string =~ s{^\s+}{}sgio;
			$component_string =~ s{\s+$}{}sgio;
		
			# Rempove repeating whitespaces
			$component_string =~ s{\s+}{ }sgio;

			# Split this so that each identifier is separated from all syntax elements
			# The identifier can start with a lowercase letter or an underscore , plus quotes

			$component_string =~ s{([_a-z0-9'"`\{\}\$\[\]]+)}{|$1|}sgio;

			# Revert overzealous splitting that splits things like _varchar(32) into several tokens
		
			$component_string =~ s{([a-z0-9_]+)\|\(\|(\d+)\|\)}{$1($2)|}sgo;

			# Remove leading and trailing pipes
			$component_string =~ s{^\|}{}sgio;
			$component_string =~ s{\|$}{}sgio;

			if (
				(exists $components{$component_string}) &&
				($grammar->[GRAMMAR_FLAGS] & GRAMMAR_FLAG_COMPACT_RULES)
			) {
				next;
			} else {
				$components{$component_string}++;
			}

			my @component_parts = split (m{\|}, $component_string);

			if (
				(grep { $_ eq $rule_name } @component_parts) &&
				($grammar->[GRAMMAR_FLAGS] & GRAMMAR_FLAG_SKIP_RECURSIVE_RULES)
			) {
				say("Skipping recursive production in rule '$rule_name'.") if rqg_debug();
				next;
			}

			#
			# If this grammar rule contains Perl code, assemble it between the various
			# component parts it was split into. This "reconstructive" step is definitely bad design
			# The way to do it properly would be to tokenize the grammar using a full-blown lexer
			# which should hopefully come up in a future version.
			#

			my $nesting_level = 0;
			my $pos = 0;
			my $code_start;

			while (1) {
				if ($component_parts[$pos] =~ m{\{}so) {
					$code_start = $pos if $nesting_level == 0;	# Code segment starts here
					my $bracket_count = ($component_parts[$pos] =~ tr/{//);
					$nesting_level = $nesting_level + $bracket_count;
				}
				
				if ($component_parts[$pos] =~ m{\}}so) {
					my $bracket_count = ($component_parts[$pos] =~ tr/}//);
					$nesting_level = $nesting_level - $bracket_count;
					if ($nesting_level == 0) {
						# Resemble the entire Perl code segment into a single string
						splice(@component_parts, $code_start, ($pos - $code_start + 1) , join ('', @component_parts[$code_start..$pos]));
						$pos = $code_start + 1;
						$code_start = undef;
					}
				}
				last if $pos > $#component_parts;
				$pos++;
			}

			push @components, \@component_parts;
		}

		my $rule = GenTest::Grammar::Rule->new(
			name => $rule_name,
			components => \@components
		);
		$rules{$rule_name} = $rule;
	}

	$grammar->[GRAMMAR_RULES] = \%rules;
	return STATUS_OK;
}

sub rule {
	return $_[0]->[GRAMMAR_RULES]->{$_[1]};
}

sub rules {
	return $_[0]->[GRAMMAR_RULES];
}

sub deleteRule {
	delete $_[0]->[GRAMMAR_RULES]->{$_[1]};
}

#
# Check if the grammar is tagged with query properties such as RESULTSET_ or ERROR_1234
#

sub hasProperties {
	if ($_[0]->[GRAMMAR_STRING] =~ m{RESULTSET_|ERROR_|QUERY_}so) {
		return 1;
	} else {
		return 0;
	}
}

##
## Make a new grammar using the patch_grammar to replace old rules and
## add new rules.
##
sub patch {
    my ($self, $patch_grammar) = @_;

    my $patch_rules = $patch_grammar->rules();

    my $rules = $self->rules();

    foreach my $ruleName (keys %$patch_rules) {
        $rules->{$ruleName} = $patch_rules->{$ruleName};
    }

    my $new_grammar = GenTest::Grammar->new(grammar_rules => $rules);
    return $new_grammar;
}


sub firstMatchingRule {
    my ($self, @ids) = @_;
    foreach my $x (@ids) {
        return $self->rule($x) if defined $self->rule($x);
    }
    return undef;
}

##
## The "body" of topGrammar
##

sub topGrammarX {
    my ($self, $level, $max, @rules) = @_;
    if ($max > 0) {
        my $result={};
        foreach my $rule (@rules) {
            foreach my $c (@{$rule->components()}) {
                my @subrules = ();
                foreach my $cp (@$c) {
                    push @subrules,$self->rule($cp) if defined $self->rule($cp);
                }
                my $componentrules = 
                    $self->topGrammarX($level + 1, $max -1,@subrules);
                if (defined  $componentrules) {
                    foreach my $sr (keys %$componentrules) {
                        $result->{$sr} = $componentrules->{$sr};
                    }
                }
            }
            $result->{$rule->name()} = $rule;
        }
        return $result;
    } else {
        return undef;
    }
}


##
## Produce a new grammar which is the toplevel $level rules of this
## grammar
##

sub topGrammar {
    my ($self, $levels, @startrules) = @_;

    my $start = $self->firstMatchingRule(@startrules);

    my $rules = $self->topGrammarX(0,$levels, $start);

    return GenTest::Grammar->new(grammar_rules => $rules);
}

##
## Produce a new grammar keeping a masked set of rules. The mask is 16
## bits. If the mask is too short, we use the original mask as a seed
## for a random number generator and generate more 16-bit values as
## needed. The mask is applied in alphapetical order on the rules to
## ensure a deterministicresult since I don't trust the Perl %hashes
## to be always ordered the same twhen they are produced e.g. from
## topGrammar or whatever...
##


sub mask {
    my ($self, $mask) = @_;


    my $rules = $self->rules();

    my %newRuleset;

    my $i = 0;
    my $prng = GenTest::Random->new(seed => $mask);
    ## Generate the first 16 bits.
    my $mask16 = $prng->uint16(0,0x7fff);
    foreach my $rulename (sort keys %$rules) {
        my $rule = $self->rule($rulename);
        my @newComponents;
        foreach my $x (@{$rule->components()}) {
            push @newComponents, $x if (1 << ($i++)) & $mask16;
            if ($i % 16 == 0) {
                # We need more bits!
                $i = 0;
                $mask = $prng->uint16(0,0x7fff);
            }
        }
        
        my $newRule;

        ## If no components were chosen, we chose all to have a working
        ## grammar.
        if ($#newComponents < 0) {
            $newRule = $rule;
        } else {
            $newRule= GenTest::Grammar::Rule->new(name => $rulename,
                                              components => \@newComponents);
        }
        $newRuleset{$rulename}= $newRule;
        
    }

    return GenTest::Grammar->new(grammar_rules => \%newRuleset);
}

1;
