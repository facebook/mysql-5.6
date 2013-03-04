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

package GenTest::Generator;

# For the sake of simplicity, all GENERATOR_* properties are defined here
# even though most of them would pertain only to GenTest::Generator::FromGrammar

require Exporter;

@ISA = qw(Exporter GenTest);

@EXPORT = qw(
	GENERATOR_GRAMMAR_FILE
	GENERATOR_GRAMMAR_STRING
	GENERATOR_GRAMMAR
	GENERATOR_SEED
	GENERATOR_PRNG
	GENERATOR_TMPNAM
	GENERATOR_THREAD_ID
	GENERATOR_SEQ_ID
	GENERATOR_MASK
	GENERATOR_MASK_LEVEL
	GENERATOR_VARCHAR_LENGTH
	GENERATOR_MASKED_GRAMMAR
	GENERATOR_GLOBAL_FRAME
	GENERATOR_PARTICIPATING_RULES
);

use strict;

use constant GENERATOR_GRAMMAR_FILE     => 0;
use constant GENERATOR_GRAMMAR_STRING   => 1;
use constant GENERATOR_GRAMMAR          => 2;
use constant GENERATOR_SEED             => 3;
use constant GENERATOR_PRNG             => 4;
use constant GENERATOR_TMPNAM           => 5;
use constant GENERATOR_THREAD_ID        => 6;
use constant GENERATOR_SEQ_ID           => 7;
use constant GENERATOR_MASK             => 8;
use constant GENERATOR_MASK_LEVEL	=> 9;
use constant GENERATOR_VARCHAR_LENGTH	=> 10;
use constant GENERATOR_MASKED_GRAMMAR	=> 11;
use constant GENERATOR_GLOBAL_FRAME	=> 12;
use constant GENERATOR_PARTICIPATING_RULES => 13;       # Stores the list of rules used in the last generated query

sub new {
	my $class = shift;
	my $generator = $class->SUPER::new({
		'grammar_file'		=> GENERATOR_GRAMMAR_FILE,
		'grammar_string'	=> GENERATOR_GRAMMAR_STRING,
		'grammar'		=> GENERATOR_GRAMMAR,
		'seed'			=> GENERATOR_SEED,
		'prng'			=> GENERATOR_PRNG,
		'thread_id'		=> GENERATOR_THREAD_ID,
		'mask'			=> GENERATOR_MASK,
		'mask_level'		=> GENERATOR_MASK_LEVEL,
		'varchar_length'	=> GENERATOR_VARCHAR_LENGTH		
	}, @_);

	return $generator;
}

sub prng {
	return $_[0]->[GENERATOR_PRNG];
}

sub grammar {
	return $_[0]->[GENERATOR_GRAMMAR];
}

sub grammarFile {
	return $_[0]->[GENERATOR_GRAMMAR_FILE];
}

sub grammarString {
	return $_[0]->[GENERATOR_GRAMMAR_STRING];
}

sub threadId {
	return $_[0]->[GENERATOR_THREAD_ID];
}

sub seqId {
	return $_[0]->[GENERATOR_SEQ_ID];
}

sub mask {
	return $_[0]->[GENERATOR_MASK];
}

sub maskLevel {
	return $_[0]->[GENERATOR_MASK_LEVEL];
}

sub maskedGrammar {
	return $_[0]->[GENERATOR_MASKED_GRAMMAR];
}

1;
