package GenTest::Transform::RemoveIndexHints;

require Exporter;
@ISA = qw(GenTest GenTest::Transform);

use strict;
use lib 'lib';

use GenTest;
use GenTest::Transform;
use GenTest::Constants;

sub transform {
	my ($class, $orig_query) = @_;

	if ($orig_query !~ m{(FORCE|IGNORE|USE)\s*KEY}sio) {
		return STATUS_WONT_HANDLE;
	} else {
		$orig_query =~ s{(FORCE|IGNORE|USE)\s+KEY\s*\(.*?\)}{}sio;
		return $orig_query." /* TRANSFORM_OUTCOME_UNORDERED_MATCH */";
	}
}

1;
