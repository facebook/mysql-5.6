query:
	SELECT returns ;

returns_list:
	returns , returns , returns |
	returns , returns , returns_list ;

returns:
	returns_integer | returns_bool | returns_string	;

returns_integer:
	ST_DIMENSION( geometry ) |
	SRID( geometry ) |
	CAST( ST_X( point ) AS INTEGER ) |
	CAST( ST_Y( point ) AS INTEGER ) |
	CAST( ST_LENGTH( linestring ) AS INTEGER ) |
	CAST( ST_NUMPOINTS( linestring ) AS INTEGER ) |
	CAST( ST_LENGTH( multilinestring ) AS INTEGER ) |
	CAST( ST_ISCLOSED( multilinestring ) AS INTEGER ) |
	CAST( ST_AREA( polygon ) AS INTEGER ) |
	ST_NUMINTERIORRINGS( polygon ) |
	CAST( ST_AREA( multipolygon ) AS INTEGER ) |
	ST_NUMGEOMETRIES( geometry_collection ) |
	CAST(ST_DISTANCE( geometry , geometry ) AS INTEGER );

returns_bool:
#	MBRContains( geometry , geometry ) |
#	MBRDisjoint( geometry , geometry ) |
#	MBREqual( geometry , geometry ) |
#	MBRIntersects( geometry , geometry ) |
#	MBROverlaps( geometry , geometry ) |
#	MBRTouches( geometry , geometry ) |
#	MBRWithin( geometry , geometry ) |
	ST_INTERSECTS( geometry , geometry ) |
	ST_CROSSES( geometry , geometry ) |
	ST_EQUALS( geometry , geometry ) |
	ST_WITHIN( geometry_1d , geometry_2d ) |
	ST_WITHIN( geometry_2d , geometry_2d ) |
	ST_CONTAINS( geometry_1d , geometry_2d ) |
	ST_CONTAINS( geometry_2d , geometry_2d ) ;
	ST_DISJOINT( geometry , geometry ) |
	ST_TOUCHES( geometry , geometry ) ;

returns_string:
	/*executor1 ASTEXT( */ /*executor2 ST_ASEWKT( */ geometry ) |
	GeometryType( geometry );

point:
	/*executor1 POINTFROMTEXT(' */ /*executor2 ST_POINTFROMTEXT(' */ point_wkt ') |
	/*executor1 POINTFROMTEXT(' */ /*executor2 ST_POINTFROMTEXT(' */ point_wkt ') |
	/*executor1 POINTFROMTEXT(' */ /*executor2 ST_POINTFROMTEXT(' */ point_wkt ') |
	/*executor1 ENDPOINT( */ /*executor2 ST_ENDPOINT( */ linestring ) |
	/*executor1 POINTN( */ /*executor2 ST_POINTN( */ linestring , returns_integer ) |
	/*executor1 STARTPOINT( */ /*executor2 ST_STARTPOINT( */ linestring ) ;

multipoint:
	/*executor1 MULTIPOINTFROMTEXT(' */ /*executor2 ST_MPOINTFROMTEXT(' */ multipoint_wkt ') ;

linestring:
	/*executor1 LINESTRINGFROMTEXT(' */ /*executor2 ST_LINEFROMTEXT(' */ linestring_wkt ') |
	/*executor1 LINESTRINGFROMTEXT(' */ /*executor2 ST_LINEFROMTEXT(' */ linestring_wkt ') |
	/*executor1 EXTERIORRING( */ /*executor2 ST_EXTERIORRING( */ polygon ) |
	/*executor1 INTERIORRINGN( */ /*executor2 ST_INTERIORRINGN( */ polygon , returns_integer ) ;

multilinestring:
	/*executor1 MULTILINESTRINGFROMTEXT(' */ /*executor2 ST_MLINEFROMTEXT(' */ multilinestring_wkt ') ;
	
polygon:
	/*executor1 POLYGONFROMTEXT(' */ /*executor2 ST_POLYGONFROMTEXT(' */ polygon_wkt ') |
	/*executor1 POLYGONFROMTEXT(' */ /*executor2 ST_POLYGONFROMTEXT(' */ polygon_wkt ') |
	ST_ENVELOPE( geometry_2d ) ;

multipolygon:
	/*executor1 MULTIPOLYGONFROMTEXT(' */ /*executor2 ST_MPOLYFROMTEXT(' */ multipolygon_wkt ') ;

geometry:
	geometry_1d | geometry_2d |
	/*executor1 GEOMETRYFROMTEXT(' */ /*executor2 ST_GEOMETRYFROMTEXT(' */ geometry_wkt ') |
	/*executor1 GEOMETRYN( */ /*executor2 ST_GEOMETRYN( */ geometry_collection , returns_integer ) |
	geometry_collection ;

geometry_1d:
	linestring | multilinestring ;

geometry_1d_disabled:
	point | linestring | multipoint | multilinestring |
	point | linestring | multipoint | multilinestring |
	point | linestring | multipoint | multilinestring |
	point | linestring | multipoint | multilinestring |
	point | linestring | multipoint | multilinestring |
	point | linestring | multipoint | multilinestring |
	ST_UNION(  geometry_1d , geometry_1d ) |
	ST_INTERSECTION( geometry_1d , geometry_1d ) |
	ST_DIFFERENCE( geometry_1d , geometry_1d ) |
	ST_SYMDIFFERENCE( geometry_1d , geometry_1d ) ;

geometry_2d:
	polygon | multipolygon | polygon | multipolygon |
	polygon | multipolygon | polygon | multipolygon |
	polygon | multipolygon | polygon | multipolygon |
	polygon | multipolygon | polygon | multipolygon |
	polygon | multipolygon | polygon | multipolygon |
	polygon | multipolygon | polygon | multipolygon |
	polygon | multipolygon | polygon | multipolygon |
	polygon | multipolygon | polygon | multipolygon |
	polygon | multipolygon | polygon | multipolygon |
	ST_BUFFER( geometry_2d , returns_integer ) |
	ST_UNION(  geometry_2d , geometry_2d )  |
	ST_INTERSECTION( geometry_2d , geometry_2d ) |
	ST_DIFFERENCE( geometry_2d , geometry_2d ) |
	ST_SYMDIFFERENCE( geometry_2d , geometry_2d ) |
	ST_ENVELOPE( geometry_2d ) 
;

geometry_collection:
	ST_GEOMCOLLFROMTEXT(' geometrycollection_wkt ') ;

geometrycollection_wkt:
	GEOMETRYCOLLECTION( geometry_wkt_list ) ;

geometry_wkt_list:
	geometry_wkt , geometry_wkt |
	geometry_wkt , geometry_wkt_list ;

geometry_wkt:
	point_wkt | linestring_wkt | polygon_wkt |
	multipoint_wkt | multilinestring_wkt | multipolygon_wkt ;

point_wkt:
	POINT( coord coord );

linestring_wkt:
	LINESTRING( point_list ) ;

point_list:
	coord coord , coord coord , coord coord , coord coord |
	coord coord , point_list ;

point_arg:
	coord coord ;

polygon_wkt:
	POLYGON( actual_polygon ) |
	POLYGON( actual_polygon ) |
	POLYGON( ( start_point , point_list , end_point ) ) |
	POLYGON( ( start_point , point_list , end_point ) ) |
	POLYGON( line_list ) ;

start_point:
	{ $start_x = $prng->digit() ; $start_y = $prng->digit() ; "$start_x $start_y"; } ;

end_point:
	{ "$start_x $start_y" } ;

multipoint_wkt:
	MULTIPOINT( point_list ) ;

multilinestring_wkt:
	MULTILINESTRING( line_list ) ;

multipolygon_wkt:
	MULTIPOLYGON( actual_polygon_list ) |
	MULTIPOLYGON( actual_polygon_list ) |
	MULTIPOLYGON( actual_polygon_list ) |
	MULTIPOLYGON( ( line_list ) ) ;

actual_polygon_list:
	( actual_polygon ) |
	( actual_polygon ) , ( actual_polygon ) |
	( actual_polygon ) , actual_polygon_list ;

actual_polygon:
	( 0 0 , point_arg , point_arg , 0 0 )  |
	( 9 9 , point_arg , point_arg , 9 9 )  |
	( 2 2 , coord 2 , point_arg , 2 coord , 2 2 )  |
	( 7 7 , coord 7 , point_arg , 7 coord , 7 7 )  |
	( 2 2 , 2 8 , 8 8 , 8 2 , 2 2 ) , ( 4 4 , 4 6 , 6 6 , 6 4 , 4 4 ) |	# doughnut
	(3 5, 2 5, 2 4, 3 4, 3 5) | 						# small square
	(3 5, 2 4, 2 5, 3 5) |							# small triangle
	digit_wkt ;

digit_wkt:
	zero | 
	one |
	two |
	three |
	four |
	five | 
	six |
	seven | 
	eight |
	nine ;

line_list:
	actual_polygon |
	( point_list ) , ( point_list ) |
	( point_list ) , line_list ;

coord:
	_digit ;

one:
	(1 5, 3 5, 3 0, 2 0, 2 4, 1 4, 1 5);

two:
	(0 5, 3 5, 3 2 , 1 2, 1 1 , 3 1, 3 0 , 0 0, 0 3, 2 3, 2 4, 0 4, 0 5);

three:
	(0 5, 3 5, 3 0, 0 0, 0 1, 2 1, 2 2 , 1 2 , 1 3 , 2 3 , 2 4, 0 4 , 0 5);

four:
	(0 5, 1 5, 1 3, 2 3, 2 5, 3 5, 3 0, 2 0 , 2 2 , 0 2, 0 5);

five:
	(0 5, 3 5, 3 4, 1 4, 1 3, 3 3 , 3 0, 0 0, 0 1, 2 1 , 2 2 , 0 2, 0 5);

six:
	(0 5, 3 5, 3 4, 1 4 , 1 3 , 3 3 , 3 0 , 0 0 , 0 5), ( 1 1 , 2 1 , 2 2 , 1 2 , 1 1 );

seven:
	(0 5, 3 5, 3 4, 2 0 , 1 0, 2 4 , 0 4, 0 5);

eight:
	(0 5, 3 5, 3 0, 0 0, 0 5), ( 1 1 , 2 1 , 2 2 , 1 2 , 1 1 ), ( 1 3 , 2 3 , 2 4 , 1 4, 1 3);

nine:
	(0 5, 3 5, 3 0, 0 0, 0 1, 2 1, 2 2, 0 2, 0 5), ( 1 3 , 2 3 , 2 4 , 1 4, 1 3);

zero:
	(0 5, 3 5, 3 0, 0 0, 0 5), ( 1 1 , 2 1 , 2 4, 1 4, 1 1 );
