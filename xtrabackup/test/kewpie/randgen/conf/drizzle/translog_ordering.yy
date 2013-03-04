# translog_ordering.yy
# grammar designed to probe for differences between transaction ordering
# within Innodb and the transaction log
#
# Inspired by:
# http://www.facebook.com/note.php?note_id=386328905932
#
# We are concerned with finding cases where such as:
# Innodb = UPDATE...SET col_int=1 WHERE col_char='a';
#          DELETE WHERE col_int=1;
# translog = DELETE WHERE col_int=1;
#            UPDATE...
# different states !

query:
  query_type ; SELECT col_int FROM _table ;

query_type:
  transaction1 | transaction2 ;

transaction1:
  BEGIN ; UPDATE _table SET `col_int` = `col_int` + 1 WHERE col_int_not_null_key = _digit[invariant] ; UPDATE _table SET `col_int_key` = `col_int` WHERE col_int_not_null_key = _digit[invariant] ; COMMIT ;

transaction2:
   BEGIN ; UPDATE _table SET `col_int` = `col_int` + 1 WHERE col_int_not_null_key = _digit[invariant] ; UPDATE _table SET `col_int_not_null` = `col_int` WHERE col_int_not_null_key = _digit[invariant] ; COMMIT ;
