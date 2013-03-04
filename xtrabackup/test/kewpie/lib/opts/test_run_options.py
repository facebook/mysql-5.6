#! /usr/bin/env python
# -*- mode: python; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
#
# Copyright (C) 2010, 2011 Patrick Crews
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA



"""Processes command line options for Drizzle test-runner"""

import os
import sys
import copy
import exceptions
import optparse

# functions
def comma_list_split(option, opt, value, parser):
    """Callback for splitting input expected in list form"""
    cur_list = getattr(parser.values, option.dest,[])
    input_list = value.split(',')
    # this is a hack to work with make target - we
    # don't deal with a dangling ',' in our list
    if '' in input_list:
        input_list.remove('')
    if cur_list:
        value_list = cur_list + input_list 
    else:
        value_list = input_list 
    setattr(parser.values, option.dest, value_list)

def get_abspath(option, opt, value, parser):
    """ Utility function to make sure we have absolute paths
        if the user supplies values

    """
    the_path = os.path.abspath(value)
    setattr(parser.values, option.dest, the_path)

def organize_options(args, test_cases):
    """Put our arguments in a nice dictionary
       We use option.dest as dictionary key
       item = supplied input
 ['
    """
    variables = {}
    # we make a copy as the python manual on vars
    # says we shouldn't alter the dictionary returned
    # by vars() - could affect symbol table?
    variables = copy.copy(vars(args))
    variables['test_cases']= test_cases
    # This code should become a function once
    # enough thought has been given to it
    if variables['manualgdb']:
        variables['gdb']=True
    if variables['repeat'] <= 0:
        print "Setting --repeat=1.  You chose a silly value that I will ignore :P"
        variables['repeat'] = 1
    # we disable the secure-file-priv option if not using dtr / mtr
    # this might need to be changed in the future...
    if variables['mode'] not in ['dtr','mtr']:
        print "Setting --no-secure-file-priv=True for randgen usage..."
        variables['nosecurefilepriv']=True
    if variables['mode'] == 'cleanup':
        print "Setting --start-dirty=True for cleanup mode..."
        variables['startdirty']=True
    if variables['libeatmydata'] and os.path.exists(variables['libeatmydatapath']):
        # We are using libeatmydata vs. shared mem for server speedup
        print "Using libeatmydata at %s.  Setting --no-shm / not using shared memory for testing..." %(variables['libeatmydatapath'])
        variables['noshm']=True
    return variables

def populate_defaults(variables, basedir_default):
    """ We fill in any default values that need
        to be put in post-parsing

    """
    if not variables['basedir']:
        # We populate this value with the default now
        # it allows us to have a default and have user
        # supplied opts to override them
        variables['basedir'].append(basedir_default)
    return variables

def handle_user_opts(variables, defaults):
    """ Some variables are dependent upon default values
        We do the probably hacky thing of going through
        and updating them accordingly

        We make the assumption / decision that only
        the first basedir value supplied should
        be applicable when searching for tests

    """
    master_basedir = os.path.abspath(variables['basedir'][0].split(':type:')[0])
    if master_basedir != defaults['basedir']:
        new_path = os.path.join(master_basedir, 'plugin')
        search_path = os.path.join(defaults['basedir'],'plugin')
        tmp = variables['suitepaths']
        tmp[tmp.index(search_path)] = new_path
        variables['suitepaths'] = tmp
    if variables['testdir'] != defaults['testdir']:
        new_path = os.path.join(variables['testdir'],'suite')
        search_path = os.path.join(defaults['testdir'],'suite')
        tmp = variables['suitepaths']
        tmp[tmp.index(search_path)] = new_path
        variables['suitepaths'] = tmp
    return variables

def parse_qp_options(defaults):
    """ We parse our options and do our magic based on some default values """
    # Create the CLI option parser
    parser= optparse.OptionParser(version='%prog (database quality platform aka project steve austin) version 0.1.1')
    config_control_group = optparse.OptionGroup(parser, 
                         "Configuration controls - allows you to specify a file with a number of options already specified")
    config_control_group.add_option(
       "--sys-config"
        , dest="sysconfigfilepath"
        , action='store'
        , default=None # We want to have a file that will be our default defaults file...
        , help="The file that specifies system configuration specs for kewpie to execute tests"
        )
    parser.add_option_group(config_control_group)


    system_control_group = optparse.OptionGroup(parser, 
                             "Options for the test-runner itself - defining the system under test and how to execute tests")

    system_control_group.add_option(
          "--force"
        , dest="force"
        , action="store_true"
        , default=False
        , help="Set this to continue test execution beyond the first failed test"
        )

    system_control_group.add_option(
           "--start-and-exit"
         , dest="startandexit"
         , action="store_true"
         , default=False
         , help="Spin up the server(s) for the first specified test then exit (will leave servers running)"
         )

    system_control_group.add_option(
           "--verbose"
         , dest="verbose"
         , action="store_true"
         , default = False
         , help="Produces extensive output about test-runner state.  Distinct from --debug"
         )
   
    system_control_group.add_option(
           "--debug"
         , dest="debug"
         , action="store_true"
         , default = False
         , help="Provide internal-level debugging output.  Distinct from --verbose"
         )

    system_control_group.add_option(
           "--mode"
         , dest="mode"
         , default="native"
         , help="Testing mode.  We currently support dtr, randgen, sysbench, sqlbench, crashme and cleanup modes.  See docs for further details about individual modes [%default]"
         )

    system_control_group.add_option(
           "--record"
         , dest="record"
         , action="store_true"
         , default=False
         , help="Record a testcase result (if the testing mode supports it) [%default]"
         )

    system_control_group.add_option(
           "--fast"
         , dest="fast"
         , action="store_true"
         , default=False
         , help="Don't try to cleanup from earlier runs (currently just a placeholder) [%default]"
         )
   
    parser.add_option_group(system_control_group)

    test_exec_control_group = optparse.OptionGroup(parser,
                                       "Options for controlling how tests are executed")

    test_exec_control_group.add_option(
        "--test-debug"
      , dest="testdebug"
      , action="store_true"
      , default=False
      , help="Toggle to control any debugging / helper output with unittest test cases [%default]"
      )

    test_exec_control_group.add_option(
        "--randgen-seed"
      , dest="randgenseed"
      , type='string'
      , action="store"
      , default='1'
      , help="Alter the seed value provided to the random query generator to vary test runs. (string) [%default]"
      )

    test_exec_control_group.add_option(
        "--opt-matrix"
      , dest="optmatrix"
      , default=''
      , help="Specify custom options for tests in --opt-matrix=\"option1=value1,option2=value2\" format"
      )

    parser.add_option_group(test_exec_control_group)

    test_control_group = optparse.OptionGroup(parser, 
                             "Options for controlling which tests are executed")

    test_control_group.add_option(
        "--suite"
      , dest="suitelist"
      , type='string'
      , action="callback"
      , callback=comma_list_split
      , default = defaults['suitelist']
      , help="The name of the suite containing tests we want. Can accept comma-separated list (with no spaces).  Additional --suite args are appended to existing list     [autosearch]"
      )

    test_control_group.add_option(
        "--suitepath"
      , dest="suitepaths"
      , type='string'
      , action="append"
      , default = defaults['suitepaths']
      , help="The path containing the suite(s) you wish to execute.  Use one --suitepath for each suite you want to use. [%default]"
      )

    test_control_group.add_option(
        "--do-test"
      , dest="dotest"
      , type='string'
      , default = None
      , help="input can either be a prefix or a regex. Will only execute tests that match the provided pattern"
      )

    test_control_group.add_option(
        "--skip-test"
      , dest="skiptest"
      , type='string'
      , default = None
      , help = "input can either be a prefix or a regex.  Will exclude tests that match the provided pattern"
      )

    test_control_group.add_option(
        "--reorder"
      , dest="reorder"
      , action="store_true"
      , default=False
      , help = "sort the testcases so that they are executed optimally for the given mode [%default]"
      )

    test_control_group.add_option(
        "--repeat"
      , dest="repeat"
      , type='int'
      , action="store"
      , default=1
      , help = "Run each test case the specified number of times.  For a given sequence, the first test will be run n times, then the second, etc [%default]"
      )

    parser.add_option_group(test_control_group)

    # test subject control group
    # terrible name for options tht define the server / code
    # that is under test
    test_subject_control_group = optparse.OptionGroup(parser,
                                     "Options for defining the code that will be under test")

    test_subject_control_group.add_option(
        "--basedir"
      , dest="basedir"
      , type='string'
      , default = []
      , action="append"
      , help = "Pass this argument to signal to the test-runner that this is an in-tree test.  We automatically set a number of variables relative to the argument (client-bindir, serverdir, testdir) [%defaults['basedir']]"
      )

    test_subject_control_group.add_option(
        "--default-server-type"
      , dest="defaultservertype"
      , type='string'
      , default = defaults['server_type']
      , action='store'
      , help = "Defines what we consider to be the default server type.  We assume a server is default type unless specified otherwise. [%default]"
      )

    test_subject_control_group.add_option(
        "--serverdir"
      , dest="serverpath"
      , type='string'
      , action="callback"
      , callback=get_abspath
      , help = "Path to the server executable.  [%default]"
      )

    test_subject_control_group.add_option(
        "--client-bindir"
      , dest="clientbindir"
      , type = 'string'
      , action="callback"
      , callback=get_abspath
      , help = "Path to the directory containing client program binaries for use in testing [%default]"
      )


    test_subject_control_group.add_option(
        "--default-storage-engine"
       , dest="defaultengine"
       , default = 'innodb'
       , help="Start drizzled using the specified engine [%default]"
       )


    parser.add_option_group(test_subject_control_group)
    # end test subject control group

    # environment options

    environment_control_group = optparse.OptionGroup(parser, 
                                "Options for defining the testing environment")

    environment_control_group.add_option(
        "--testdir"
      , dest="testdir"
      , type = 'string'
      , default = defaults['testdir']
      , action="callback"
      , callback=get_abspath
      , help = "Path to the test dir, containing additional files for test execution. [%default]"
      )

    environment_control_group.add_option(
        "--workdir"
      , dest="workdir"
      , type='string'
      , default = defaults['workdir']
      , action="callback"
      , callback=get_abspath
      , help = "Path to the directory test-run will use to store generated files and directories. [%default]"
      )

    environment_control_group.add_option(
        "--top-srcdir"
      , dest="topsrcdir"
      , type='string'
      , default = defaults['basedir']
      , help = "build option [%default]"
      )

    environment_control_group.add_option(
        "--top-builddir"
      , dest="topbuilddir"
      , type='string'
      , default = defaults['basedir']
      , help = "build option [%default]"
      )

    environment_control_group.add_option(
        "--no-shm"
      , dest="noshm"
      , action='store_true'
      , default=defaults['noshm']
      , help = "By default, we symlink workdir to a location in shm.  Use this flag to not symlink [%default]"
      )

    environment_control_group.add_option(
        "--libeatmydata"
      , dest="libeatmydata"
      , action='store_true'
      , default=False
      , help = "We use libeatmydata (if available) to disable fsyncs and speed up test execution.  Implies --no-shm"
      )

    environment_control_group.add_option(
        "--libeatmydata-path"
      , dest="libeatmydatapath"
      , action='store'
      , default='/usr/local/lib/libeatmydata.so'
      , help = "Path to the libeatmydata install you want to use [%default]"
      )

    environment_control_group.add_option(
        "--start-dirty"
      , dest="startdirty"
      , action='store_true'
      , default=False
      , help = "Don't try to clean up working directories before test execution [%default]"
      )

    environment_control_group.add_option(
        "--no-secure-file-priv"
      , dest = "nosecurefilepriv"
      , action='store_true'
      , default=False
      , help = "Turn off the use of --secure-file-priv=vardir for started servers"
      )

    environment_control_group.add_option(
           "--randgen-path"
         , dest="randgenpath"
         , action='store'
         , default=defaults['randgen_path']
         , help = "The path to a randgen installation that can be used to execute randgen-based tests"
         )

    environment_control_group.add_option(
           "--innobackupex-path"
         , dest="innobackupexpath"
         , action='store'
         , default=defaults['innobackupexpath']
         , help = "The path to the innobackupex script that facilitates the use of Xtrabackup"
         )

    environment_control_group.add_option(
          "--xtrabackup-path"
        , dest="xtrabackuppath"
        , action='store'
        , default=defaults['xtrabackuppath']
        , help = "The path the xtrabackup binary to be tested"
        )

    environment_control_group.add_option(
        "--tar4ibd-path"
      , dest="tar4ibdpath"
      , action='store'
      , default=defaults['tar4ibdpath']
      , help="The path to the tar4ibd binary that will be used for any applicable tests"
      )

    environment_control_group.add_option(
          "--wsrep-provider-path"
       , dest="wsrepprovider"
       , action='store'
       , default=defaults['wsrep_provider_path']
       , help = "The path to a wsrep provider library for use with mysql"
       )

    environment_control_group.add_option(
          "--cluster-cnf"
        , dest="clustercnf"
        , action='store'
        , default=None
        , help = "The path to a config file defining a running cluster (node info)"
        )

    environment_control_group.add_option(
          "--subunit-outfile"
        , dest="subunitoutfile"
        , action='store'
        , default=defaults['subunit_file']
        , help = "File path where subunit output will be logged [%default]"
        )

    parser.add_option_group(environment_control_group)
    # end environment control group

    option_passing_group = optparse.OptionGroup(parser,
                          "Options to pass options on to the server")

    option_passing_group.add_option(
    "--drizzled"
      , dest="drizzledoptions"
      , type='string'
      , action='append' 
      , default = []
      , help = "Pass additional options to the server.  Will be passed to all servers for all tests (mostly for --start-and-exit)"
      )

    parser.add_option_group(option_passing_group)
    # end option passing group

    analysis_control_group = optparse.OptionGroup(parser, 
                                "Options for defining the tools we use for code analysis (valgrind, gprof, gcov, etc)")

    analysis_control_group.add_option(
        "--valgrind"
      , dest="valgrind"
      , action='store_true'
      , default = False
      , help = "Run drizzletest and drizzled executables using valgrind with default options [%default]"
      )

    analysis_control_group.add_option(
        "--valgrind-option"
      , dest="valgrindarglist"
      , type='string'
      , action="append"
      , help = "Pass an option to valgrind (overrides/removes default valgrind options)"
      )

    analysis_control_group.add_option(
        "--valgrind-suppressions"
      , dest="valgrindsuppressions"
      , type='string'
      , action='store'
      , default = defaults['valgrind_suppression']
      , help = "Point at a valgrind suppression file [%default]"
      )

    analysis_control_group.add_option(
        "--helgrind"
      , dest="helgrind"
      , action='store_true'
      , default=False
      , help="Use the helgrind tool for valgrind.  Implies / will auto-use --valgrind"
      )

    parser.add_option_group(analysis_control_group)

    debugger_control_group = optparse.OptionGroup(parser,
                               "Options for controlling the use of debuggers with test execution")

    debugger_control_group.add_option(
        "--gdb"
      , dest="gdb"
      , action='store_true'
      , default=False
      , help="Start the drizzled server(s) in gdb"
      )

    debugger_control_group.add_option(
        "--manual-gdb"
      , dest="manualgdb"
      , action='store_true'
      , default=False
      , help="Allows you to start the drizzled server(s) in gdb manually (in another window, etc)"
      )

    parser.add_option_group(debugger_control_group)

    utility_group = optparse.OptionGroup(parser,
                      "Options to call additional utilities such as datagen")

    utility_group.add_option(
        "--gendata"
      , dest="gendatafile"
      , action='store'
      , type='string'
      , default=None
      , help="Call the randgen's gendata utility to use the specified configuration file.  This will populate the server prior to any test execution")

    parser.add_option_group(utility_group)

    # supplied will be those arguments matching an option, 
    # and test_cases will be everything else
    (args, test_cases)= parser.parse_args()

    variables = {}
    variables = organize_options(args, test_cases)
    variables = populate_defaults(variables, defaults['basedir'])
    variables = handle_user_opts(variables, defaults)
    return variables
