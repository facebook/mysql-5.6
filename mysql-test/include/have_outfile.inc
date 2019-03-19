let $outfile= `SELECT LOAD_FILE(CONCAT(@tmpdir, '/outfile.test')) LIKE 'Outfile OK%'`;
remove_file $MYSQL_TMP_DIR/outfile.test;

if (!$outfile)
{
  --skip Test requires 'have_outfile'
}
