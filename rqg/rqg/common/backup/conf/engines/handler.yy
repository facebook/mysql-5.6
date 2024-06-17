query_init:
	HANDLER _table OPEN AS alias1 ; HANDLER _table OPEN AS alias2 ; HANDLER _table OPEN AS alias3 ; HANDLER _table OPEN AS alias4 ;

query:
	handler | handler | handler | handler | handler |
	handler | handler | handler | handler | handler |
	handler | handler | handler | handler | handler |
	handler | handler | handler | handler | handler |
	non_handler ;

thread1:
	handler | handler | handler | handler | handler |
	handler | handler | handler | handler | handler |
	handler | handler | handler | handler | handler |
	non_handler | administrative ;

handler:
	handler_sequence |
	handler_random ;

non_handler:
	insert | update ;

insert:
	INSERT IGNORE INTO _table ( _field , _field ) VALUES ( value , value ) ;

update:
	UPDATE IGNORE _table SET _field = value where ;

handler_random:
	handler_open_close |
	handler_read ;

handler_sequence:
	handler_open_close ; handler_read_list ;

handler_read_list:
	handler_read ; handler_read |
	handler_read ; handler_read_list ;

handler_open_close:
	HANDLER alias1 CLOSE ; HANDLER _table OPEN AS alias1 |
	HANDLER alias2 CLOSE ; HANDLER _table OPEN AS alias2 |
	HANDLER alias3 CLOSE ; HANDLER _table OPEN AS alias3 |
	HANDLER alias4 CLOSE ; HANDLER _table OPEN AS alias4 ;

alias:
	alias1 | alias2 | alias3 | alias4 ;

handler_read:
	handler_read_unprepared | handler_read_unprepared |
	DEALLOCATE PREPARE h_r ; PREPARE h_r FROM " handler_read_unprepared " ; EXECUTE h_r |
	DEALLOCATE PREPARE h_rp ; PREPARE h_rp FROM " HANDLER alias READ index_name comp_op ( ? ) where limit " ; SET @val = value ; EXECUTE h_rp USING @val |
	DEALLOCATE PREPARE h_rp2 ; PREPARE h_rp2 FROM " HANDLER alias READ index_name index_op WHERE _field comp_op ? " ; SET @val = value ; EXECUTE h_rp2 USING @val ;

handler_read_unprepared:
	HANDLER alias READ index_name comp_op ( value ) where limit |
	HANDLER alias READ index_name index_op where limit |
	HANDLER alias READ first_next where limit ;

comp_op:
	= | <= | >= | < | > ;

index_op:
	FIRST | NEXT | PREV | LAST ;

index_name:
	`PRIMARY` | _field_key ;

first_next:
	FIRST | NEXT ;

value:
	_digit | _tinyint_unsigned | _varchar(1) ;

limit:
	| | | | | LIMIT _digit ;

where:
	| WHERE _field comp_op value ;

administrative:
	FLUSH TABLES |
	ALTER TABLE _table ADD COLUMN filler VARCHAR(255) DEFAULT ' filler ' |
	ALTER TABLE _table DROP COLUMN filler ;
