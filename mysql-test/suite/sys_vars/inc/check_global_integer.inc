#############################################################################
# ==== Purpose ====                                                         #
#                                                                           #
# Check if global integer $VARIABLE_NAME returns errors if it's outside a   #
# range between $VARIABLE_MIN_VALUE (default: 0) and $VARIABLE_MAX_VALUE.   #
#                                                                           #
# Additionally we check the behaviour of this variable for:                 #
# * Default values                                                          #
# * Valid and invalid values                                                #
# * Scope and access method                                                 #
#                                                                           #
# References: http://dev.mysql.com/doc/refman/8.0/en/                       #
#                        server-system-variables.html                       #
#                                                                           #
# ==== Usage ====                                                           #
#                                                                           #
# --let $VARIABLE_NAME = 'NAME'                                             #
# --let $VARIABLE_MAX_VALUE = NUMBER                                        #
# [--let $VARIABLE_MIN_VALUE = NUMBER ]                                     #
# [--let $VARIABLE_BLOCK_SIZE = NUMBER ]                                    #
# --source suite/sys_vars/inc/check_global_integer.inc                      #
#                                                                           #
# ==== Examples ====                                                        #
#                                                                           #
# --let $VARIABLE_NAME = slowlog_space_limit                                #
# --let $VARIABLE_MAX_VALUE = 18446744073709551615                          #
# --source suite/sys_vars/inc/check_global_integer.inc                      #
#                                                                           #
# --let $VARIABLE_NAME = slave_pending_jobs_size_max                        #
# --let $VARIABLE_MIN_VALUE = 1024                                          #
# --let $VARIABLE_BLOCK_SIZE = 1024                                         #
# --let $VARIABLE_MAX_VALUE = 18446744073709551615                          #
# --source suite/sys_vars/inc/check_global_integer.inc                      #
#############################################################################

################################################################
# Initialize auxiliary varaibles                               #
################################################################

--let $GLOBAL_VAR_NAME = @@global.$VARIABLE_NAME
--let $SESSION_VAR_NAME = @@session.$VARIABLE_NAME

--let $CHECK_MIN_VALUE = 0
if ($VARIABLE_MIN_VALUE)
{
--let $CHECK_MIN_VALUE = $VARIABLE_MIN_VALUE
}

--let $CHECK_BLOCK_SIZE = 1
if ($VARIABLE_BLOCK_SIZE)
{
  --let $CHECK_BLOCK_SIZE = $VARIABLE_BLOCK_SIZE
}
--let $CHECK_MIN_VALUE_PLUS_BLOCK_SIZE = `SELECT $CHECK_MIN_VALUE + $CHECK_BLOCK_SIZE`

################################################################
# Saving initial values of $VARIABLE_NAME variable in          #
# a temporary variable                                         #
################################################################

--eval SET @save_original_variable = $GLOBAL_VAR_NAME
SELECT @save_original_variable;

################################################################
# Display the DEFAULT value of $VARIABLE_NAME                  #
################################################################

--eval SET $GLOBAL_VAR_NAME = $CHECK_MIN_VALUE_PLUS_BLOCK_SIZE
--eval SET $GLOBAL_VAR_NAME = DEFAULT
--eval SELECT $GLOBAL_VAR_NAME

--error ER_GLOBAL_VARIABLE
--eval SET $SESSION_VAR_NAME = $CHECK_MIN_VALUE + 2
--error ER_GLOBAL_VARIABLE
--eval SET $SESSION_VAR_NAME = DEFAULT
--error ER_INCORRECT_GLOBAL_LOCAL_VAR
--eval SELECT $SESSION_VAR_NAME

################################################################
# Check if NULL or empty value is accepted                     #
################################################################

--error ER_WRONG_TYPE_FOR_VAR
--eval SET $GLOBAL_VAR_NAME = NULL

--error ER_WRONG_TYPE_FOR_VAR
--eval SET $GLOBAL_VAR_NAME = ''

--error ER_WRONG_TYPE_FOR_VAR
--eval SET $GLOBAL_VAR_NAME = ' '

--error ER_GLOBAL_VARIABLE
--eval SET $SESSION_VAR_NAME = NULL

--error ER_GLOBAL_VARIABLE
--eval SET $SESSION_VAR_NAME = ''

--error ER_GLOBAL_VARIABLE
--eval SET $SESSION_VAR_NAME = ' '

#############################################################################
# Change the value of $VARIABLE_NAME to a valid value for GLOBAL scope      #
#############################################################################

--eval SET $GLOBAL_VAR_NAME = $CHECK_MIN_VALUE
--eval SELECT $GLOBAL_VAR_NAME

--eval SET $GLOBAL_VAR_NAME = $CHECK_MIN_VALUE_PLUS_BLOCK_SIZE
--eval SELECT $GLOBAL_VAR_NAME

if ($CHECK_MIN_VALUE == 0)
{
 if ($CHECK_BLOCK_SIZE == 1)
 {
  --eval SET $GLOBAL_VAR_NAME = TRUE
  --eval SELECT $GLOBAL_VAR_NAME

  --eval SET $GLOBAL_VAR_NAME = FALSE
  --eval SELECT $GLOBAL_VAR_NAME
 }
}

--eval SET $GLOBAL_VAR_NAME = $VARIABLE_MAX_VALUE
--eval SELECT $GLOBAL_VAR_NAME

--eval SET $GLOBAL_VAR_NAME = DEFAULT
--eval SELECT $GLOBAL_VAR_NAME

#############################################################################
# Change the value of $VARIABLE_NAME to an invalid value for GLOBAL scope   #
#############################################################################

--eval SET $GLOBAL_VAR_NAME = -$CHECK_MIN_VALUE_PLUS_BLOCK_SIZE
--eval SELECT $GLOBAL_VAR_NAME

--error ER_WRONG_TYPE_FOR_VAR
--eval SET $GLOBAL_VAR_NAME = 'ABC'
--eval SELECT $GLOBAL_VAR_NAME

--error ER_WRONG_TYPE_FOR_VAR
--eval SET $GLOBAL_VAR_NAME = ON
--eval SELECT $GLOBAL_VAR_NAME

--error ER_WRONG_TYPE_FOR_VAR
--eval SET $GLOBAL_VAR_NAME = 'OFF'
--eval SELECT $GLOBAL_VAR_NAME

--error ER_WRONG_TYPE_FOR_VAR
--eval SET $GLOBAL_VAR_NAME = $CHECK_MIN_VALUE_PLUS_BLOCK_SIZE.14
--eval SELECT $GLOBAL_VAR_NAME

--error ER_WRONG_TYPE_FOR_VAR
--eval SET $GLOBAL_VAR_NAME = NONE
--eval SELECT $GLOBAL_VAR_NAME

if ($VARIABLE_MAX_VALUE == 18446744073709551615)
{
--error ER_WRONG_TYPE_FOR_VAR
}
--eval SET $GLOBAL_VAR_NAME = -$VARIABLE_MAX_VALUE
--eval SELECT $GLOBAL_VAR_NAME

if ($VARIABLE_MAX_VALUE == 18446744073709551615)
{
--error ER_DATA_OUT_OF_RANGE
}
--eval SET $GLOBAL_VAR_NAME = $VARIABLE_MAX_VALUE + 1
--eval SELECT $GLOBAL_VAR_NAME

#############################################################################
# Check if the value in GLOBAL Table matches value in variable              #
#############################################################################

--let $assert_text= Must return 1
--let $assert_cond= $GLOBAL_VAR_NAME = VARIABLE_VALUE FROM performance_schema.global_variables WHERE VARIABLE_NAME="$VARIABLE_NAME"
--source include/assert.inc

#############################################################################
# Check if $VARIABLE_NAME variable can be accessed with and without @@ sign #
#############################################################################

--eval SET GLOBAL $VARIABLE_NAME = $CHECK_MIN_VALUE_PLUS_BLOCK_SIZE
--eval SET $GLOBAL_VAR_NAME = $CHECK_MIN_VALUE_PLUS_BLOCK_SIZE

--error ER_PARSE_ERROR
--eval SET global.$VARIABLE_NAME = $CHECK_MIN_VALUE_PLUS_BLOCK_SIZE

--error ER_GLOBAL_VARIABLE
--eval SET @@$VARIABLE_NAME = $CHECK_MIN_VALUE_PLUS_BLOCK_SIZE

--error ER_GLOBAL_VARIABLE
--eval SET $VARIABLE_NAME = $CHECK_MIN_VALUE_PLUS_BLOCK_SIZE

--error ER_UNKNOWN_TABLE
--eval SELECT global.$VARIABLE_NAME

--error ER_BAD_FIELD_ERROR
--eval SELECT GLOBAL $VARIABLE_NAME

--eval SELECT @@$VARIABLE_NAME

#############################################################################
# Restoring the original value of $VARIABLE_NAME variable                   #
#############################################################################

--eval SET $GLOBAL_VAR_NAME = @save_original_variable
