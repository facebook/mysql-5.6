--exec export ZSTD=`command -v zstd`
--let $zstd_exists=ZSTD
if(!$zstd_exists)
{
  skip test requires zstd program to be installed in the system, skipping test;
}

# test compression of random data, 20000 rows
# mysqldump creates 2 compressed files
CREATE TABLE t1(x VARCHAR(100));
# test empty table
CREATE TABLE t2(x VARCHAR(100));
# test compression of random data, 200000 rows
# mysqldump creates 12 compressed files
CREATE TABLE t3(x VARCHAR(100));
# test compression of repeated data with many newlines
CREATE TABLE t4(x VARCHAR(1000));
insert into t4 values (concat("<<<", REPLACE(REPEAT("abc.", 100), ".", "\n"), ">>>"));
insert into t4 select t4.* from t4, t4 t42, t4 t43, t4 t44;
insert into t4 select t4.* from t4, t4 t42, t4 t43, t4 t44;
insert into t4 select t4.* from t4, t4 t42, t4 t43, t4 t44;

--let $tmp_dir=`SELECT @@GLOBAL.secure_file_priv`
--let $generated_input_file=$tmp_dir/t1.txt
--let $generated_big_input_file=$tmp_dir/t3.txt

# generate input data
--exec dd bs=20000000 count=1 if=/dev/urandom | base64 -w 80 > $tmp_dir/t1.tmp
--exec head -n 20000  $tmp_dir/t1.tmp | tee $generated_input_file | wc
--exec head -n 200000  $tmp_dir/t1.tmp | tee $generated_big_input_file | wc
--remove_file $tmp_dir/t1.tmp

# load input data
--replace_result $generated_input_file INPUT_FILE
--eval LOAD DATA LOCAL INFILE '$generated_input_file' INTO TABLE t1;
--replace_result $generated_big_input_file INPUT_FILE
--eval LOAD DATA LOCAL INFILE '$generated_big_input_file' INTO TABLE t3;

# show data in tables
select count(*) from t1;
select count(*) from t2;
select count(*) from t3;
select count(*) from t4;
