query:
	SELECT AsText(linestring_nokey) FROM linestring /*executor1 FORCE KEY ( linestring_key ) */  WHERE where_cond AND ST_Length( linestring_nokey ) > 1;

where_cond:
	bool_cond |
	where_cond and_or bool_cond |
	bool_cond and_or bool_cond ;

and_or:
	AND | AND | AND | AND | AND |
	AND | AND | AND | AND | OR ;

bool_cond:
        ST_INTERSECTS( geometry , geometry_column ) |
       ST_CROSSES( geometry , geometry_column ) |
	ST_CONTAINS( GeomFromText('POLYGON( polygon_wkt )') , geometry_column ) |
	ST_EQUALS( ( SELECT linestring_nokey FROM linestring WHERE pk = pk_value ) , geometry_column ) |
	ST_WITHIN( GeomFromText('POLYGON( polygon_wkt )') , geometry_column ) |
	ST_DISJOINT( geometry , geometry_column ) |
	ST_TOUCHES( geometry , geometry_column ) ;
;

geometry_column:
	linestring_key | linestring_key | linestring_key | linestring_key | linestring_nokey ;

geometry:
	GeomFromText('LINESTRING( point_list )') |
	GeomFromText('MULTILINESTRING( line_list )') ;
#|
#	( SELECT linestring_nokey FROM linestring WHERE pk = pk_value ) ;

line_list:
	( point_list ) , ( point_list ) |
	( point_list ) , line_list ;

point_list:
	point , point , point |
	point , point , point_list ;

polygon_wkt:
	( start_point , point_list , end_point ) ;

point:
	{ $prng->int(3000,5000)." ".$prng->int(2000,4000) } |
	{ $prng->int(3750,4250)." ".$prng->int(2750,3250) } |
	{ $prng->int(4000,4100)." ".$prng->int(3000,3100) } ;

pk_value_list:
	pk_value , pk_value |
	pk_value , pk_value_list ;

pk_value:
	{ $prng->int(1,3726) } ;

start_point:
        { $start_x = $prng->int(3750,4250) ; $start_y = $prng->int(2750,3250) ; "$start_x $start_y"; } ;

end_point:
        { "$start_x $start_y" };
