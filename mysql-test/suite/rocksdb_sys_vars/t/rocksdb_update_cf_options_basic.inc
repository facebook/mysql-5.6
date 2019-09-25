USE information_schema;

# We should start with NULL.
SELECT @@global.rocksdb_update_cf_options;

# ... and we should be able to handle NULL and issue a reasonable warning.
SET @@global.rocksdb_update_cf_options = NULL;
SELECT @@global.rocksdb_update_cf_options;

# Make sure that we do not double free the NULL string
SET @@global.rocksdb_update_cf_options = NULL;
SELECT @@global.rocksdb_update_cf_options;

# Attempt setting an empty string
SET @@global.rocksdb_update_cf_options = '';
SELECT @@global.rocksdb_update_cf_options;

# Will fail to parse. Value not updated.
--Error ER_WRONG_VALUE_FOR_VAR
--eval SET @@global.rocksdb_update_cf_options = 'aaaaa';
SELECT @@global.rocksdb_update_cf_options;

SELECT * FROM ROCKSDB_CF_OPTIONS WHERE CF_NAME='default' AND OPTION_TYPE='WRITE_BUFFER_SIZE';
SELECT * FROM ROCKSDB_CF_OPTIONS WHERE CF_NAME='default' AND OPTION_TYPE='TARGET_FILE_SIZE_BASE';

# Save these off to reset later
--let $ORIG_WRITE_BUFFER_SIZE=`SELECT VALUE FROM ROCKSDB_CF_OPTIONS WHERE CF_NAME='default' AND OPTION_TYPE='WRITE_BUFFER_SIZE'`
--let $ORIG_TARGET_FILE_SIZE_BASE=`SELECT VALUE FROM ROCKSDB_CF_OPTIONS WHERE CF_NAME='default' AND OPTION_TYPE='TARGET_FILE_SIZE_BASE'`
--let $ORIG_MAX_BYTES_FOR_LEVEL_MULTIPLIER=`SELECT VALUE FROM ROCKSDB_CF_OPTIONS WHERE CF_NAME='default' AND OPTION_TYPE='MAX_BYTES_FOR_LEVEL_MULTIPLIER'`

# All good. Use default CF.
SET @@global.rocksdb_update_cf_options = 'default={write_buffer_size=8m;target_file_size_base=2m};';
SELECT @@global.rocksdb_update_cf_options;

SELECT * FROM ROCKSDB_CF_OPTIONS WHERE CF_NAME='default' AND OPTION_TYPE='WRITE_BUFFER_SIZE';
SELECT * FROM ROCKSDB_CF_OPTIONS WHERE CF_NAME='default' AND OPTION_TYPE='TARGET_FILE_SIZE_BASE';

SELECT * FROM ROCKSDB_CF_OPTIONS WHERE CF_NAME='cf1' AND OPTION_TYPE='WRITE_BUFFER_SIZE';
SELECT * FROM ROCKSDB_CF_OPTIONS WHERE CF_NAME='cf1' AND OPTION_TYPE='TARGET_FILE_SIZE_BASE';

SELECT * FROM ROCKSDB_CF_OPTIONS WHERE CF_NAME='cf2' AND OPTION_TYPE='WRITE_BUFFER_SIZE';
SELECT * FROM ROCKSDB_CF_OPTIONS WHERE CF_NAME='cf2' AND OPTION_TYPE='MAX_BYTES_FOR_LEVEL_MULTIPLIER';

SELECT * FROM ROCKSDB_CF_OPTIONS WHERE CF_NAME='cf3' AND OPTION_TYPE='TARGET_FILE_SIZE_BASE';

# All good. Use multiple valid CF-s.
SET @@global.rocksdb_update_cf_options = 'cf1={write_buffer_size=8m;target_file_size_base=2m};cf2={write_buffer_size=16m;max_bytes_for_level_multiplier=8};cf3={target_file_size_base=4m};';
SELECT @@global.rocksdb_update_cf_options;

SELECT * FROM ROCKSDB_CF_OPTIONS WHERE CF_NAME='cf1' AND OPTION_TYPE='WRITE_BUFFER_SIZE';
SELECT * FROM ROCKSDB_CF_OPTIONS WHERE CF_NAME='cf1' AND OPTION_TYPE='TARGET_FILE_SIZE_BASE';

SELECT * FROM ROCKSDB_CF_OPTIONS WHERE CF_NAME='cf2' AND OPTION_TYPE='WRITE_BUFFER_SIZE';
SELECT * FROM ROCKSDB_CF_OPTIONS WHERE CF_NAME='cf2' AND OPTION_TYPE='MAX_BYTES_FOR_LEVEL_MULTIPLIER';

SELECT * FROM ROCKSDB_CF_OPTIONS WHERE CF_NAME='cf3' AND OPTION_TYPE='TARGET_FILE_SIZE_BASE';

# All good. Use a single valid CF.
SET @@global.rocksdb_update_cf_options = 'cf3={target_file_size_base=24m};';
SELECT @@global.rocksdb_update_cf_options;

SELECT * FROM ROCKSDB_CF_OPTIONS WHERE CF_NAME='cf3' AND OPTION_TYPE='TARGET_FILE_SIZE_BASE';

# Some parts are good. Value still updated.
SET @@global.rocksdb_update_cf_options = 'cf1={target_file_size_base=24m};foo={max_bytes_for_level_multiplier=8};';
SELECT @@global.rocksdb_update_cf_options;

SELECT * FROM ROCKSDB_CF_OPTIONS WHERE CF_NAME='cf1' AND OPTION_TYPE='TARGET_FILE_SIZE_BASE';

# Will fail to parse. No valid assignments included. Value not updated and
# reset to NULL.
--Error ER_WRONG_VALUE_FOR_VAR
--eval SET @@global.rocksdb_update_cf_options = 'default={foo=bar};';
SELECT @@global.rocksdb_update_cf_options;

# Reset the cf options so the test passes with --repeat=2
--eval SET @@global.rocksdb_update_cf_options = 'default={write_buffer_size=$ORIG_WRITE_BUFFER_SIZE;target_file_size_base=$ORIG_TARGET_FILE_SIZE_BASE};'
--eval SET @@global.rocksdb_update_cf_options = 'cf1={write_buffer_size=$ORIG_WRITE_BUFFER_SIZE;target_file_size_base=$ORIG_TARGET_FILE_SIZE_BASE};'
--eval SET @@global.rocksdb_update_cf_options = 'cf2={write_buffer_size=$ORIG_WRITE_BUFFER_SIZE;target_file_size_base=$ORIG_TARGET_FILE_SIZE_BASE;max_bytes_for_level_multiplier=$ORIG_MAX_BYTES_FOR_LEVEL_MULTIPLIER};'
--eval SET @@global.rocksdb_update_cf_options = 'cf3={write_buffer_size=$ORIG_WRITE_BUFFER_SIZE;target_file_size_base=$ORIG_TARGET_FILE_SIZE_BASE};'

SET @@global.rocksdb_update_cf_options = NULL;
SELECT @@global.rocksdb_update_cf_options;  
