package My::Constants;

use strict;
use warnings;

require Exporter;
our @ISA    = qw( Exporter );
our @EXPORT = qw( MTR_SKIP_BY_FRAMEWORK MTR_SKIP_BY_TEST );

use constant MTR_SKIP_BY_FRAMEWORK => 'MTR_SKIP_BY_FRAMEWORK';
use constant MTR_SKIP_BY_TEST => 'MTR_SKIP_BY_TEST';
