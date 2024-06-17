use strict;
use DBI;
$| = 1;

my $osm_dbi = 'dbi:mysql:user=root:database=osm';
my $oqg_dbi = 'dbi:mysql:host=127.0.0.1:port=9306:database=test:user=root';

my $osm_dbh = DBI->connect($osm_dbi, undef , undef , { RaiseError => 1});
my $oqg_dbh = DBI->connect($oqg_dbi, undef , undef , { RaiseError => 1});

my $ways = $osm_dbh->selectcol_arrayref("SELECT DISTINCT id FROM ways");

$oqg_dbh->do("CREATE TABLE IF NOT EXISTS osm (    latch   SMALLINT  UNSIGNED NULL,    origid  BIGINT    UNSIGNED NULL,    destid  BIGINT    UNSIGNED NULL,    weight  DOUBLE    NULL,    seq     BIGINT    UNSIGNED NULL,    linkid  BIGINT    UNSIGNED NULL,    KEY (latch, origid, destid) USING HASH,    KEY (latch, destid, origid) USING HASH  ) ENGINE=OQGRAPH");

print "$#$ways found.\n";

foreach my $way (@$ways) {
	my $nodes = $osm_dbh->selectcol_arrayref("SELECT node_id FROM way_nodes WHERE id = $way ORDER BY sequence_id");
#	print "$#$nodes found for way $way\n";

	if ($#$nodes > 1) {
		$oqg_dbh->do("
			INSERT INTO osm ( origid, destid ) VALUES ".
			join(', ', map { "( $nodes->[$_] , $nodes->[$_ + 1] )" } (0..($#$nodes-1)))
		);
		print "*";
	} else {
		print ".";
	}
}
