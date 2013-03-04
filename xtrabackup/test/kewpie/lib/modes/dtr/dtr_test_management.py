#! /usr/bin/env python
# -*- mode: python; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
#
# Copyright (C) 2010 Patrick Crews
#
## This program is free software; you can redistribute it and/or modify
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

""" dtr_test_management:
    code related to the gathering / analysis / management of 
    the test cases
    ie - collecting the list of tests in each suite, then
    gathering additional, relevant information for the test-runner's dtr
    mode. (traditional diff-based testing)

"""

# imports
import os
import re
import sys
from ConfigParser import RawConfigParser

import lib.test_mgmt.test_management as test_management


    
class testCase:
    """Holds info on a per .test file basis
       Attributes contain information necessary to execute / validate
       the test file when it is executed.
 
    """
    def __init__(self, system_manager, test_case=None, test_name=None, suite_name=None
                 , suite_path=None, test_server_options=[], test_path=None, result_path=None
                 , comment=None, master_sh=None, cnf_path=None
                 , disable=0, innodb_test=1 
                 , need_debug=0, debug=0):
        self.system_manager = system_manager
        self.logging = self.system_manager.logging
        self.skip_keys = ['system_manager']
        self.testcase = test_case
        self.testname = test_name
        self.suitename = suite_name
        self.suitepath = suite_path
        self.fullname = "%s.%s" %(suite_name, test_name)
        self.testpath = test_path
        self.resultpath = result_path

        self.skip_flag = 0
        self.timezone = "GMT-3"
        self.component_id = "drizzled"
        self.slave_count = 0
        self.master_count = 1
        self.server_options = test_server_options
        # We will populate this in a better fashion later on
        # as we allow .cnf files, we need to do a bit of
        # messing about to make this work right
        if self.server_options == [] or type(self.server_options[0]) is not list:
            self.server_requirements=[self.server_options]
        else:
            self.server_requirements = self.server_options
            self.server_options= self.server_options[0][0]
        self.comment = comment
        self.master_sh = master_sh
        self.cnf_path = cnf_path
        self.disable = disable
        self.innodb_test  = innodb_test
        self.need_debug = need_debug
        
        self.system_manager.logging.debug_class(self)

    def should_run(self):
        if self.skip_flag or self.disable:
            return 0
        else:
            return 1

 
        
        
          
class testManager(test_management.testManager):
    """Deals with scanning test directories, gathering test cases, and 
       collecting per-test information (opt files, etc) for use by the
       test-runner

    """

    def process_suite(self,suite_dir):
        """Process a test suite.
           This includes searching for tests in test_list and only
           working with the named tests (all tests in suite is the default)
           Further processing includes reading the disabled.def file
           to know which tests to skip, processing the suite.opt file,
           and processing the individual test cases for data relevant
           to the rest of the test-runner
        
        """

        self.system_manager.logging.verbose("Processing suite: %s" %(suite_dir))

        # Generate our test and result files:
        testdir = os.path.join(suite_dir, 't')
        resultdir = os.path.join(suite_dir, 'r')

        # Do basic checks to make sure this is worth further work
        self.check_suite(suite_dir, testdir, resultdir)      

        # Get suite-level options
        suite_options = []
        cnf_path, suite_options = self.process_suite_options(suite_dir) 

        # Get the 'name' of the suite.  This can require some processing
        # But the name is useful for reporting and whatnot
        suite_name = self.get_suite_name(suite_dir)
        
        # Get the contents of the testdir and filter it accordingly
        # This applies do-test / skip-test filters and any specific
        # test case names        
        testlist = self.testlist_filter(os.listdir(testdir))
        # sort our list if no individual tests were specified
        if not self.desired_tests:
            testlist.sort()
                       
        # gather deeper information on tests we are interested in
        if testlist:
            # We have tests we want to process, we gather additional information
            # that is useful at the suite level.  This includes disabled tests          
            # Gather disabled test list.  
            # This is used in process_test_file()
            disabled_tests = {}
            disabled_tests = self.process_disabled_test_file(testdir)                        
            for test_case in testlist:
                self.add_test(self.process_test_file(suite_dir,
                                              suite_name, cnf_path, suite_options
                                              , disabled_tests, testdir 
                                              , resultdir, test_case))

    def process_test_file(self, suite_dir, suite_name, suite_cnf_path, suite_options 
                          , disabled_tests, testdir 
                          , resultdir, test_case):
        """ We generate / find / store all the relevant information per-test.
            This information will be used when actually executing the test
            We store the data in a testCase object 

        """
        
        test_server_options = suite_options
        test_name = test_case.replace('.test','')
        self.system_manager.logging.verbose("Processing test: %s.%s" %(suite_name,test_name))

        
        # Fix this , create a testCase with gather_test_data() passed
        # as the argument
        # Ensure we pass everything we need and use it all
        (  test_path
         , result_file_name
         , result_path
         , comment
         , master_sh
         , cnf_path
         , test_server_options
         , disable
         , innodb_test
         , need_debug) = self.gather_test_data(test_case, test_name,
                                 suite_name, test_server_options,testdir, 
                                 resultdir, disabled_tests)
        if suite_cnf_path and not cnf_path:
            cnf_path=suite_cnf_path
        test_case = testCase(self.system_manager, test_case, test_name, suite_name, 
                             suite_dir, test_server_options,test_path, result_path,
                             master_sh=master_sh, cnf_path=cnf_path, debug=self.debug)      
        return test_case


########################################################################
# utility functions
#
# Stuff that helps us out and simplifies our main functions
# But isn't that important unless you need to dig deep
########################################################################

    def gather_test_data(self, test_case, test_name, suite_name,
                         test_server_options, testdir, resultdir, disabled_tests):
        """ We gather all of the data needed to produce a testCase for
            a given test

        """

        test_path = os.path.join(testdir,test_case)
        result_file_name = test_name+'.result'       
        result_path = self.find_result_path(resultdir, result_file_name)
        comment = None
        master_sh_path = test_path.replace('.test','-master.sh')
        if os.path.exists(master_sh_path):
            master_sh = master_sh_path 
        else:
            master_sh = None
        master_opt_path = test_path.replace('.test', '-master.opt')
        config_file_path = test_path.replace('.test', '.cnf')
        # NOTE:  this currently makes suite level server options additive 
        # to file-level .opt files...not sure if this is the best.
        test_server_options = test_server_options + self.process_opt_file(
                                                           master_opt_path)
        # deal with .cnf files (which supercede master.opt stuff)
        cnf_options = []
        cnf_flag, returned_options = self.process_cnf_file(config_file_path)
        cnf_options += returned_options
        if cnf_flag: # we found a proper file and need to override
            found_options = cnf_options
        else:
            config_file_path = None
        (disable, comment) = self.check_if_disabled(disabled_tests, test_name)
        innodb_test = 0
        need_debug = 0
        return (test_path, result_file_name, result_path, comment, master_sh, 
                config_file_path, test_server_options, disable, innodb_test, need_debug)

    def check_suite(self, suite_dir, testdir, resultdir):
        """Handle basic checks of the suite:
           does the suite exist?
           is there a /t dir?
           is there a /r dir?
           Error and die if not

        """
        # We expect suite to be a path name, no fuzzy searching
        if not os.path.exists(suite_dir):
            self.system_manager.logging.error("Suite: %s does not exist" %(suite_dir))
            sys.exit(1)
                
        # Ensure our test and result directories are present
        if not os.path.exists(testdir):
            self.system_manager.logging.error("Suite: %s does not have a 't' directory (expected location for test files)" %(suite_dir))
            sys.exit(1)
        if not os.path.exists(resultdir):
            self.system_manager.logging.error("Suite: %s does not have an 'r' directory (expected location for result files)" %(suite_dir))
            sys.exit(1)

    def get_suite_name(self, suite_dir):
        """ Get the 'name' of the suite
            This can either be the path basename or one directory up, as
            in the case of files in the drizzle/plugins directory
        
        """

        # We trim any trailing path delimiters as python returns
        # '' for basedir if the path ends that way
        # BEGIN horrible hack to accomodate bad location of main suite : /
        if suite_dir == self.testdir:
            return 'main'
        # END horrible hack : /
        if suite_dir.endswith('/'):
            suite_dir=suite_dir[:-1]
        suite_dir_root,suite_dir_basename = os.path.split(suite_dir)
        if suite_dir_basename == 'tests' or suite_dir_basename == 'drizzle-tests':
            suite_name = os.path.basename(suite_dir_root)
        else:
            suite_name = suite_dir_basename
        self.system_manager.logging.debug("Suite_name:  %s" %(suite_name))
        return suite_name

    def process_suite_options(self, suite_dir):
        """ Process the suite.opt and master.opt files
            that reside at the suite-level if they exist.
            Return a list of the options found

            We also process .cnf files - this
            is currently dbqp-only and is the proper
            way to do things :P

        """
        found_options = []
        opt_files = ['t/master.opt','t/suite.opt']
        for opt_file in opt_files:
            found_options = found_options + self.process_opt_file(os.path.join(suite_dir,opt_file))
        # We also process the suite-level .cnf file(s).  We override
        # a master.opt file if we have a .cnf file.  There is no reason they
        # should ever be used in conjunction and I am biased towards .cnf ; )
        cnf_files = ['t/master.cnf']
        cnf_options = []
        for cnf_file in cnf_files:
            config_file_path = os.path.join(suite_dir,cnf_file)
            cnf_flag, returned_options = self.process_cnf_file(config_file_path)
            cnf_options += returned_options
        if cnf_flag: # we found a proper file and need to override
            found_options = cnf_options
        else:
            config_file_path = None
        return config_file_path, found_options

    def process_disabled_test_file(self, testdir):
        """ Checks and processes the suite's disabled.def
            file.  This file must reside in the suite/t directory
            It must be in the format:
            test-name : comment (eg BugNNNN - bug on hold, test disabled)
            In reality a test should *never* be disabled.  EVER.
            However, we keep this as a bit of utility

        """
        disabled_tests = {}
        disabled_def_path = os.path.join(testdir,'disabled.def')
        if not os.path.exists(disabled_def_path):
            return disabled_tests

        try:
            disabled_test_file = open(disabled_def_path,'r')
        except IOError, e: 
            self.system_manager.logging.error("Problem opening disabled.def file: %s" %(disabled_def_path))
            sys.exit(1)

        self.system_manager.logging.debug("Processing disabled.def file: %s" %(disabled_def_path))
        disabled_bug_pattern = re.compile("[\S]+[\s]+:[\s]+[\S]")
        
        for line in disabled_test_file:
            line = line.strip()
            if not line.startswith('#'): # comment
                if re.match(disabled_test_pattern,line):
                    self.system_manager.logging.debug("found disabled test - %s" %(line))
                    test_name, test_comment = line.split(':')
                    disabled_tests[test_name.strip()]=test_comment.strip() 
            
        disabled_test_file.close()
        return disabled_tests
        

    def process_opt_file(self, opt_file_path):
        """ Process a test-run '.opt' file.
        These files contain test and suite-specific server options
        (ie what options the server needs to use for the test) 

        Returns a list of the options (we don't really validate...yet)
         
        NOTE:  test-run.pl allows for server *and* system options
        (eg timezone, slave_count, etc) in opt files.  We don't.
        None of our tests use this and we should probably avoid it
        We can introduce server and system .opt files or even better
        would be to use config files as we do with drizzle-automation
        This would allow us to specify options for several different
        things in a single file, but in a clean and standardized manner
 
        """
        found_options = []
        if not os.path.exists(opt_file_path):
            return found_options

        try:
            opt_file = open(opt_file_path,'r')
        except IOError, e: 
            self.system_manager.logging.error("Problem opening option file: %s" %(opt_file_path))
            sys.exit(1)

        self.system_manager.logging.debug("Processing opt file: %s" %(opt_file_path))
        for line in opt_file:
            options = line.split('--')
            if options:
                for option in options:
                    if option:
                        if 'restart' in option or '#' in option:
                            option = 'restart'
                        found_options.append('--%s' %(option.strip()))
        opt_file.close()
        return found_options

    def process_cnf_file(self, cnf_file_path):
        """ We extract meaningful information from a .cnf file
            if it exists.  Currently limited to server allocation
            needs

        """

        server_requirements = []
        cnf_flag = 0
        if os.path.exists(cnf_file_path):
            cnf_flag = 1
            config_reader = RawConfigParser()
            config_reader.read(cnf_file_path)
            server_requirements = self.process_server_reqs(config_reader.get('test_servers','servers'))
        return ( cnf_flag, server_requirements )

    def process_server_reqs(self,data_string):
        """ We read in the list of lists as a string, so we need to 
            handle this / break it down into proper chunks

        """
        server_reqs = []
        # We expect to see a list of lists and throw away the 
        # enclosing brackets
        option_sets = data_string[1:-1].strip().split(',')
        for option_set in option_sets:
            server_reqs.append([option_set[1:-1].strip()])
        return server_reqs

    def testlist_filter(self, testlist):
        """ Filter our list of testdir contents based on several 
            criteria.  This looks for user-specified test-cases
            and applies the do-test and skip-test filters

            Returns the list of tests that we want to execute
            for further processing

        """

        # We want only .test files
        # Possible TODO:  allow alternate test extensions
        testlist = [test_file for test_file in testlist if test_file.endswith('.test')]
         
        # Search for specific test names
        if self.desired_tests: # We have specific, named tests we want from the suite(s)
           tests_to_use = []
           for test in self.desired_tests:
               if test.endswith('.test'): 
                   pass
               else:
                   test = test+'.test'
               if test in testlist:
                   tests_to_use.append(test)
           testlist = tests_to_use

        # TODO:  Allow for regex?
        # Apply do-test filter
        if self.dotest:
            testlist = [test_file for test_file in testlist if test_file.startswith(self.dotest)]
        # Apply skip-test filter
        if self.skiptest:
            testlist = [test_file for test_file in testlist if not test_file.startswith(self.skiptest)]
        return testlist

    def find_result_path(self, result_dir, result_file_name):
        """ This is copied from test-run.pl dtr_cases.pl
            If we have an engine option passed in and the 
            path resultdir/engine/testname.result exists, that is 
            our .result file
        
            Need to check if we really need this - maybe PBXT?

        """
        result_path = os.path.join(result_dir,result_file_name)
        if self.default_engine:
            candidate_path = os.path.join(result_dir, self.default_engine, 
                                          result_file_name)
            if os.path.exists(candidate_path):
                result_path = candidate_path
        return result_path

    def check_if_disabled(self, disabled_tests, test_name):
        """ Scan the list of disabled tests if it exists to see 
            if the test is disabled.
        
        """
   
        if disabled_tests:
            if test_name in disabled_tests:
                self.system_manager.logging.debug("%s says - I'm disabled" %(test_name))
                return (1, disabled_tests[test_name])
        return (0,None)

    def sort_testcases(self):
        """ We sort our testcases according to the server_options they have
            For each testcase, we sort the list of options, so if a test has
            --plugin-add=csv --abracadabra, we would get 
            --abracadabra --plugin-add=csv
            
            This results in tests that have similar options being run in order
            this minimizes server restarts which can be costly

        """
        test_management.testManager.sort_testcases(self)
        organizer = {}
        ordered_list = []
        for testcase in self.test_list:
            key = " ".join(sorted(testcase.server_options))
            if key in organizer:
                organizer[key].append(testcase)
            else:
                organizer[key] = [testcase]
        for value_list in organizer.values():
            ordered_list = ordered_list + value_list
        self.test_list = ordered_list
        

        
