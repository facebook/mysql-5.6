--disable_query_log

--output $MYSQL_TMP_DIR/A
--eval $query1

--output $MYSQL_TMP_DIR/B
--eval $query2

--enable_query_log

--diff_files $MYSQL_TMP_DIR/A $MYSQL_TMP_DIR/B

--remove_file $MYSQL_TMP_DIR/A
--remove_file $MYSQL_TMP_DIR/B
