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

# This script can be used to load the files from
# http://www.tm.kit.edu/~mayer/osm2wkt/ 
# into MySQL and PostGIS

use strict;

print '
/*

@MISC{mayer2010osm,
  author = {Christoph P. Mayer},
  title = {osm2wkt - OpenStreetMap to WKT Conversion},
  howpublished = {http://www.tm.kit.edu/~mayer/osm2wkt},
  year = {2010}
} 

Map data (c) OpenStreetMap contributors, CC-BY-SA

*/'."\n";

print "DROP TABLE IF EXISTS linestring;\n";

if ($ARGV[0] eq 'MySQL') {
	print "CREATE TABLE linestring (pk INTEGER NOT NULL PRIMARY KEY, linestring_key GEOMETRY NOT NULL, linestring_nokey GEOMETRY NOT NULL) ENGINE=Aria TRANSACTIONAL=0;\n";
} elsif ($ARGV[0] eq 'PostGIS') {
	print "CREATE TABLE linestring (pk INTEGER NOT NULL PRIMARY KEY);\n";
	print "SELECT AddGeometryColumn('linestring', 'linestring_key', -1, 'GEOMETRY', 2 );\n";
	print "SELECT AddGeometryColumn('linestring', 'linestring_nokey', -1, 'GEOMETRY', 2 );\n";
}

my $counter = 1;
while (<STDIN>) {
	chomp $_;
	print "INSERT INTO linestring (pk, linestring_key, linestring_nokey) VALUES ($counter, GeomFromText('$_'), GeomFromText('$_'));\n";
	$counter++;
}

if ($ARGV[0] eq 'MySQL') {
	print "ALTER TABLE linestring ADD SPATIAL KEY (linestring_key);\n";
} elsif ($ARGV[0] eq 'MySQL')  {
	print "CREATE INDEX linestring_index ON linestring USING GIST ( linestring_key );\n";
}
