############## mysql-test\t\lock_wait_timeout_basic.test #######################
#                                                                              #
# Variable Name: lock_wait_timeout                                             #
# Scope: GLOBAL & SESSION                                                      #
# Access Type: Dynamic                                                         #
# Data Type: Numeric                                                           #
# Default Value: 1                                                             #
# Range: 1 - 604800                                                            #
#                                                                              #
#                                                                              #
# Creation Date: 2010-02-08                                                    #
# Author:  Jon Olav Hauglid                                                    #
#                                                                              #
# Description: Test Cases of Dynamic System Variable "lock_wait_timeout"       #
#              that checks behavior of this variable in the following ways     #
#              * Default Value                                                 #
#              * Valid & Invalid values                                        #
#              * Scope & Access method                                         #
#              * Data Integrity                                                #
#                                                                              #
# Reference: http://dev.mysql.com/doc/refman/5.5/en/                           #
#  server-system-variables.html#option_mysqld_lock-wait-timeout                #
#                                                                              #
################################################################################

--source include/load_sysvars.inc

#####################################################################
#           START OF lock_wait_timeout TESTS                        #
#####################################################################

#############################################################
#                 Save initial value                        #
#############################################################

eval SET @start_global_value = @@global.$lock_wait_timeout;
SELECT @start_global_value;
if ($session)
{
  eval SET @start_session_value = @@session.$lock_wait_timeout;
  SELECT @start_session_value;
}

--echo '#--------------------FN_DYNVARS_002_01-------------------------#'
#####################################################################
#     Display the DEFAULT value of lock_wait_timeout                #
#####################################################################

eval SET @@global.$lock_wait_timeout = 100;
eval SET @@global.$lock_wait_timeout = DEFAULT;
eval SELECT @@global.$lock_wait_timeout;

if ($session)
{
  eval SET @@session.$lock_wait_timeout = 200;
  eval SET @@session.$lock_wait_timeout = DEFAULT;
  eval SELECT @@session.$lock_wait_timeout;
}

--echo '#--------------------FN_DYNVARS_002_02-------------------------#'
#####################################################################
#     Check the DEFAULT value of lock_wait_timeout                  #
#####################################################################

eval SET @@global.$lock_wait_timeout = @start_global_value;
eval SELECT @@global.$lock_wait_timeout = 604800;
if ($session)
{
  eval SET @@session.$lock_wait_timeout = @start_session_value;
  eval SELECT @@session.$lock_wait_timeout = 604800;
}

--echo '#--------------------FN_DYNVARS_002_03-------------------------#'
###############################################################################
# Change the value of lock_wait_timeout to a valid value for GLOBAL Scope     #
###############################################################################

eval SET @@global.$lock_wait_timeout = 0;
eval SELECT @@global.$lock_wait_timeout;
eval SET @@global.$lock_wait_timeout = 0.005;
eval SELECT @@global.$lock_wait_timeout;
eval SET @@global.$lock_wait_timeout = 1;
eval SELECT @@global.$lock_wait_timeout;
eval SET @@global.$lock_wait_timeout = 1.23;
eval SELECT @@global.$lock_wait_timeout;
eval SET @@global.$lock_wait_timeout = 60020;
eval SELECT @@global.$lock_wait_timeout;
eval SET @@global.$lock_wait_timeout = 65535;
eval SELECT @@global.$lock_wait_timeout;


if ($session)
{
  --echo '#--------------------FN_DYNVARS_002_04-------------------------#'
  ##############################################################################
  # Change the value of lock_wait_timeout to a valid value for SESSION Scope   #
  ##############################################################################

  eval SET @@session.$lock_wait_timeout = 0;
  eval SELECT @@session.$lock_wait_timeout;
  eval SET @@session.$lock_wait_timeout = 0.00001;
  eval SELECT @@session.$lock_wait_timeout;
  eval SET @@session.$lock_wait_timeout = 1;
  eval SELECT @@session.$lock_wait_timeout;
  eval SET @@session.$lock_wait_timeout = 1.23;
  eval SELECT @@session.$lock_wait_timeout;
  eval SET @@session.$lock_wait_timeout = 50050;
  eval SELECT @@session.$lock_wait_timeout;
  eval SET @@session.$lock_wait_timeout = 65535;
  eval SELECT @@session.$lock_wait_timeout;
}

--echo '#------------------FN_DYNVARS_002_05-----------------------#'
#################################################################
# Change the value of lock_wait_timeout to an invalid value     #
#################################################################
# for global scope
eval SET @@global.$lock_wait_timeout = -1024;
eval SELECT @@global.$lock_wait_timeout;
eval SET @@global.$lock_wait_timeout = 604801;
eval SELECT @@global.$lock_wait_timeout;
--Error ER_WRONG_TYPE_FOR_VAR
eval SET @@global.$lock_wait_timeout = ON;
eval SELECT @@global.$lock_wait_timeout;
--Error ER_WRONG_TYPE_FOR_VAR
eval SET @@global.$lock_wait_timeout = OFF;
eval SELECT @@global.$lock_wait_timeout;
--Error ER_WRONG_TYPE_FOR_VAR
eval SET @@global.$lock_wait_timeout = test;
eval SELECT @@global.$lock_wait_timeout;

# for session scope
if ($session)
{
  eval SET @@session.$lock_wait_timeout = -2;
  eval SELECT @@session.$lock_wait_timeout;
  eval SET @@session.$lock_wait_timeout = 604900;
  eval SELECT @@session.$lock_wait_timeout;

  --Error ER_WRONG_TYPE_FOR_VAR
  eval SET @@session.$lock_wait_timeout = ON;
  eval SELECT @@session.$lock_wait_timeout;
  --Error ER_WRONG_TYPE_FOR_VAR
  eval SET @@session.$lock_wait_timeout = OFF;
  eval SELECT @@session.$lock_wait_timeout;
  --Error ER_WRONG_TYPE_FOR_VAR
  eval SET @@session.$lock_wait_timeout = test;
  eval SELECT @@session.$lock_wait_timeout;
}


--echo '#------------------FN_DYNVARS_002_06-----------------------#'
####################################################################
#   Check if the value in GLOBAL Table matches value in variable   #
####################################################################

eval SELECT @@global.$lock_wait_timeout = VARIABLE_VALUE
FROM performance_schema.global_variables
WHERE VARIABLE_NAME='$lock_wait_timeout';


--echo '#------------------FN_DYNVARS_002_07-----------------------#'
####################################################################
#  Check if the value in SESSION Table matches value in variable   #
####################################################################

if ($session)
{
  eval SELECT @@session.$lock_wait_timeout = VARIABLE_VALUE
  FROM performance_schema.session_variables
  WHERE VARIABLE_NAME='$lock_wait_timeout';
}

--echo '#------------------FN_DYNVARS_002_08-----------------------#'
####################################################################
#     Check if TRUE and FALSE values can be used on variable       #
####################################################################

eval SET @@global.$lock_wait_timeout = TRUE;
eval SELECT @@global.$lock_wait_timeout;
eval SET @@global.$lock_wait_timeout = FALSE;
eval SELECT @@global.$lock_wait_timeout;


--echo '#---------------------FN_DYNVARS_001_09----------------------#'
###############################################################################
#  Check if global and session variables are independant of each other        #
###############################################################################

if ($session)
{
  eval SET @@global.$lock_wait_timeout = 10;
  eval SET @@session.$lock_wait_timeout = 11;
  eval SELECT @@$lock_wait_timeout = @@global.$lock_wait_timeout;
}

--echo '#---------------------FN_DYNVARS_001_10----------------------#'
##############################################################################
#    Check if accessing variable with SESSION,LOCAL and without SCOPE points #
#    to same session variable                                                #
##############################################################################

if ($session)
{
  eval SET @@$lock_wait_timeout = 100;
  eval SELECT @@$lock_wait_timeout = @@local.$lock_wait_timeout;
  eval SELECT @@local.$lock_wait_timeout = @@session.$lock_wait_timeout;
}

--echo '#---------------------FN_DYNVARS_001_11----------------------#'
###############################################################################
#   Check if lock_wait_timeout can be accessed with and without @@ sign       #
###############################################################################

if ($session)
{
  eval SET $lock_wait_timeout = 1;
  eval SELECT @@$lock_wait_timeout;
  --Error ER_UNKNOWN_TABLE
  eval SELECT local.$lock_wait_timeout;
  --Error ER_UNKNOWN_TABLE
  eval SELECT session.$lock_wait_timeout;
  --Error ER_BAD_FIELD_ERROR
  eval SELECT $lock_wait_timeout = @@session.$lock_wait_timeout;
}

####################################
#     Restore initial value        #
####################################

eval SET @@global.$lock_wait_timeout = @start_global_value;
eval SELECT @@global.$lock_wait_timeout;
if ($session)
{
  eval SET @@session.$lock_wait_timeout = @start_session_value;
  eval SELECT @@session.$lock_wait_timeout;
}

###################################################
#          END OF lock_wait_timeout TESTS         #
###################################################


