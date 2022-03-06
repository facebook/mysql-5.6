#############################################################
#                 Save initial value                        #
#############################################################

SET @start_global_value = @@global.min_examined_row_limit_sql_stats;
SELECT @start_global_value;
SET @start_session_value = @@session.min_examined_row_limit_sql_stats;
SELECT @start_session_value;


--echo '#--------------------FN_DYNVARS_089_01-------------------------#'
######################################################################
#     Display the DEFAULT value of min_examined_row_limit_sql_stats            #
######################################################################

SET @@global.min_examined_row_limit_sql_stats = 100;
SET @@global.min_examined_row_limit_sql_stats = DEFAULT;
SELECT @@global.min_examined_row_limit_sql_stats;

SET @@session.min_examined_row_limit_sql_stats = 200;
SET @@session.min_examined_row_limit_sql_stats = DEFAULT;
SELECT @@session.min_examined_row_limit_sql_stats;


--echo '#--------------------FN_DYNVARS_089_02-------------------------#'
########################################################################
#     Check the DEFAULT value of min_examined_row_limit_sql_stats              #
########################################################################

SET @@global.min_examined_row_limit_sql_stats = DEFAULT;
SELECT @@global.min_examined_row_limit_sql_stats = 0;

SET @@session.min_examined_row_limit_sql_stats = DEFAULT;
SELECT @@session.min_examined_row_limit_sql_stats = 0;


--echo '#--------------------FN_DYNVARS_089_03-------------------------#'
################################################################################
# Change the value of min_examined_row_limit_sql_stats to a valid value for GLOBAL Scope #
################################################################################

SET @@global.min_examined_row_limit_sql_stats = 0;
SELECT @@global.min_examined_row_limit_sql_stats;
SET @@global.min_examined_row_limit_sql_stats = 1;
SELECT @@global.min_examined_row_limit_sql_stats;
SET @@global.min_examined_row_limit_sql_stats = 60020;
SELECT @@global.min_examined_row_limit_sql_stats;
SET @@global.min_examined_row_limit_sql_stats = 65535;
SELECT @@global.min_examined_row_limit_sql_stats;
SET @@global.min_examined_row_limit_sql_stats = 4294967295;
SELECT @@global.min_examined_row_limit_sql_stats;
SET @@global.min_examined_row_limit_sql_stats = 4294967294;
SELECT @@global.min_examined_row_limit_sql_stats;

# Test cases for 64-bit machines
if($mach64)
{
  --disable_warnings
  SET @@global.min_examined_row_limit_sql_stats = 18446744073709551615;
  --enable_warnings

  # Bug#11752618 - 64-BIT WINDOWS VARIABLES LIMITED TO DIFFERENT VALUES THAN 64-BIT LINUX/SO
  let $machine_name = `SELECT CONVERT(@@version_compile_os using latin1) IN ("Win64", "Windows")`;

  if($machine_name)
  {
    --replace_result 4294967295 18446744073709551615
  }
  SELECT @@global.min_examined_row_limit_sql_stats;
}


--echo '#--------------------FN_DYNVARS_089_04-------------------------#'
###################################################################################
# Change the value of min_examined_row_limit_sql_stats to a valid value for SESSION Scope #
###################################################################################

SET @@session.min_examined_row_limit_sql_stats = 0;
SELECT @@session.min_examined_row_limit_sql_stats;
SET @@session.min_examined_row_limit_sql_stats = 1;
SELECT @@session.min_examined_row_limit_sql_stats;
SET @@session.min_examined_row_limit_sql_stats = 50050;
SELECT @@session.min_examined_row_limit_sql_stats;
SET @@session.min_examined_row_limit_sql_stats = 65535;
SELECT @@session.min_examined_row_limit_sql_stats;
SET @@session.min_examined_row_limit_sql_stats = 4294967295;
SELECT @@session.min_examined_row_limit_sql_stats;
SET @@session.min_examined_row_limit_sql_stats = 4294967294;
SELECT @@session.min_examined_row_limit_sql_stats;

# Test cases for 64-bit machines
if($mach64)
{
  --disable_warnings
  SET @@session.min_examined_row_limit_sql_stats = 18446744073709551615;
  --enable_warnings

  # Bug#11752618 - 64-BIT WINDOWS VARIABLES LIMITED TO DIFFERENT VALUES THAN 64-BIT LINUX/SO
  if($machine_name)
  {
    --replace_result 4294967295 18446744073709551615
  }
  SELECT @@session.min_examined_row_limit_sql_stats;
}

--echo '#------------------FN_DYNVARS_089_05-----------------------#'
####################################################################
# Change the value of min_examined_row_limit_sql_stats to an invalid value #
####################################################################

SET @@global.min_examined_row_limit_sql_stats = -1024;
SELECT @@global.min_examined_row_limit_sql_stats;
--Error ER_WRONG_TYPE_FOR_VAR
SET @@global.min_examined_row_limit_sql_stats = 65530.34;
SELECT @@global.min_examined_row_limit_sql_stats;
--Error ER_WRONG_TYPE_FOR_VAR
SET @@global.min_examined_row_limit_sql_stats = test;
SELECT @@global.min_examined_row_limit_sql_stats;

if($mach32)
{
  SET @@global.min_examined_row_limit_sql_stats = 4294967296;
  SELECT @@global.min_examined_row_limit_sql_stats;

  SET @@global.min_examined_row_limit_sql_stats = 42949672950;
  SELECT @@global.min_examined_row_limit_sql_stats;
}

if($mach64)
{
  --error ER_WRONG_TYPE_FOR_VAR
  SET @@global.min_examined_row_limit_sql_stats = 18446744073709551616;
  SELECT @@global.min_examined_row_limit_sql_stats;
}

SET @@session.min_examined_row_limit_sql_stats = -1;
SELECT @@session.min_examined_row_limit_sql_stats;
--Error ER_WRONG_TYPE_FOR_VAR
SET @@session.min_examined_row_limit_sql_stats = 65530.34;

--Error ER_WRONG_TYPE_FOR_VAR
SET @@session.min_examined_row_limit_sql_stats = test;
SELECT @@session.min_examined_row_limit_sql_stats;

if($mach32)
{
  SET @@session.min_examined_row_limit_sql_stats = 4294967296;
  SELECT @@session.min_examined_row_limit_sql_stats;

  SET @@session.min_examined_row_limit_sql_stats = 42949672950;
  SELECT @@session.min_examined_row_limit_sql_stats;
}

if($mach64)
{
  --error ER_WRONG_TYPE_FOR_VAR
  SET @@session.min_examined_row_limit_sql_stats = 18446744073709551616;
  SELECT @@session.min_examined_row_limit_sql_stats;
}

--echo '#------------------FN_DYNVARS_089_06-----------------------#'
####################################################################
#   Check if the value in GLOBAL Table matches value in variable   #
####################################################################

--disable_warnings
SELECT @@global.min_examined_row_limit_sql_stats = VARIABLE_VALUE
FROM performance_schema.global_variables
WHERE VARIABLE_NAME='min_examined_row_limit_sql_stats';
--enable_warnings

--echo '#------------------FN_DYNVARS_089_07-----------------------#'
####################################################################
#  Check if the value in SESSION Table matches value in variable   #
####################################################################

--disable_warnings
SELECT @@session.min_examined_row_limit_sql_stats = VARIABLE_VALUE
FROM performance_schema.session_variables
WHERE VARIABLE_NAME='min_examined_row_limit_sql_stats';
--enable_warnings

--echo '#------------------FN_DYNVARS_089_08-----------------------#'
####################################################################
#     Check if TRUE and FALSE values can be used on variable       #
####################################################################

SET @@global.min_examined_row_limit_sql_stats = TRUE;
SELECT @@global.min_examined_row_limit_sql_stats;
SET @@global.min_examined_row_limit_sql_stats = FALSE;
SELECT @@global.min_examined_row_limit_sql_stats;


--echo '#---------------------FN_DYNVARS_089_09----------------------#'
####################################################################################
#  Check if accessing variable with and without GLOBAL point to same variable   #
####################################################################################

SET @@global.min_examined_row_limit_sql_stats = 10;
SELECT @@min_examined_row_limit_sql_stats = @@global.min_examined_row_limit_sql_stats;


--echo '#---------------------FN_DYNVARS_089_10----------------------#'
########################################################################################################
#    Check if accessing variable with SESSION,LOCAL and without SCOPE points to same session variable  #
########################################################################################################

SET @@min_examined_row_limit_sql_stats = 100;
SELECT @@min_examined_row_limit_sql_stats = @@local.min_examined_row_limit_sql_stats;
SELECT @@local.min_examined_row_limit_sql_stats = @@session.min_examined_row_limit_sql_stats;


--echo '#---------------------FN_DYNVARS_089_11----------------------#'
###################################################################################
#   Check if min_examined_row_limit_sql_stats can be accessed with and without @@ sign    #
###################################################################################

SET min_examined_row_limit_sql_stats = 1;
SELECT @@min_examined_row_limit_sql_stats;
--Error ER_UNKNOWN_TABLE
SELECT local.min_examined_row_limit_sql_stats;
--Error ER_UNKNOWN_TABLE
SELECT session.min_examined_row_limit_sql_stats;
--Error ER_BAD_FIELD_ERROR
SELECT min_examined_row_limit_sql_stats = @@session.min_examined_row_limit_sql_stats;


####################################
#     Restore initial value        #
####################################

SET @@global.min_examined_row_limit_sql_stats = @start_global_value;
SELECT @@global.min_examined_row_limit_sql_stats;
SET @@session.min_examined_row_limit_sql_stats = @start_session_value;
SELECT @@session.min_examined_row_limit_sql_stats;


#############################################################
#                 END OF min_examined_row_limit_sql_stats TESTS     #
#############################################################
