let $have_boringssl= `SELECT @@have_boringssl`;
if ($have_boringssl != YES)
{
  --skip Test requires 'have_boringssl'
}
