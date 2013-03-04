#! /usr/bin/env python
# -*- mode: python; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
#
# Copyright (C) 2010 Patrick Crews
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

""" test_management:
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
import thread
          
class testManager(object):
    """Deals with scanning test directories, gathering test cases, and 
       collecting per-test information (opt files, etc) for use by the
       test-runner

    """

    def __init__( self, variables, system_manager):

        self.system_manager = system_manager
        self.time_manager = system_manager.time_manager
        self.logging = system_manager.logging
        if variables['verbose']:
            self.logging.verbose("Initializing test manager...")

        self.skip_keys = [ 'system_manager'
                         , 'verbose'
                         , 'debug'
                         ]
        self.test_list = []
        self.first_test = 1
        self.total_test_count = 0
        self.executed_tests = {} # We have a hash of 'status':[test_name..]
        self.executing_tests = {}
        self.verbose = variables['verbose']
        self.debug = variables['debug']
        self.default_engine = variables['defaultengine']
        self.dotest = variables['dotest']
        if self.dotest:
            self.dotest = self.dotest.strip()
        self.skiptest = variables['skiptest']
        if self.skiptest:
            self.skiptest = self.skiptest.strip()
        self.reorder = variables['reorder']
        self.suitelist = variables['suitelist']
        self.mode = variables['mode']
        
        self.suitepaths = variables['suitepaths']
        self.testdir = variables['testdir']
        self.desired_tests = variables['test_cases']
        
        self.logging.debug_class(self)

    def add_test(self, new_test_case):
        """ Add a new testCase to our self.test_list """

        self.test_list.append(new_test_case)
        
    def gather_tests(self):
        self.logging.info("Processing test suites...")
        # BEGIN terrible hack to accomodate the fact that
        # our 'main' suite is also our testdir : /
        if self.suitelist is None and self.mode=='dtr':
            self.suitepaths = [self.testdir]
            self.suitelist = ['main']
        # END horrible hack
        for suite in self.suitelist:
            suite_path = self.find_suite_path(suite)
            if suite_path:
                self.process_suite(suite_path)
            else:
                self.logging.error("Could not find suite: %s in any of paths: %s" %(suite, ", ".join(self.suitepaths)))
        self.process_gathered_tests()

    def process_gathered_tests(self):
        """ We do some post-gathering analysis and whatnot
            Report an error if there were desired_tests but no tests
            were found.  Otherwise just report what we found
    
        """

        # See if we need to reorder our test cases
        if self.reorder:
            self.sort_testcases()

        if self.desired_tests and not self.test_list:
            # We wanted tests, but found none
            # Probably need to make this smarter at some point
            # To maybe make sure that we found all of the desired tests...
            # However, this is a start / placeholder code
            self.logging.error("Unable to locate any of the desired tests: %s" %(" ,".join(self.desired_tests)))   
        self.total_test_count = len(self.test_list)     
        self.logging.info("Found %d test(s) for execution" %(self.total_test_count))
        
        self.logging.debug("Found tests:")
        self.logging.debug("%s" %(self.print_test_list()))

    def find_suite_path(self, suitename):
        """ We have a suitename, we need to locate the path to
            the juicy suitedir in one of our suitepaths.

            Theoretically, we could have multiple matches, but
            such things should never be allowed, so we don't
            code for it.  We return the first match.
 
            testdir can either be suitepath/suitename or
            suitepath/suitename/tests.  We test and return the
            existing path.   Return None if no match found

        """
        # BEGIN horrible hack to accomodate bad location of main suite
        if self.mode == 'dtr':
            if self.suitepaths == [self.testdir] or suitename == 'main':
                # We treat this as the 'main' suite
                return self.testdir
        # END horrible hack
        for suitepath in self.suitepaths:
            suite_path = self.system_manager.find_path([ os.path.join(suitepath,suitename,'tests'),
                                     os.path.join(suitepath,suitename) ], required = 0 )
            if suite_path:
                return suite_path
        return suite_path

    def process_suite(self,suite_dir):
        """Process a test suite.
           This includes searching for tests in test_list and only
           working with the named tests (all tests in suite is the default)
           Further processing includes reading the disabled.def file
           to know which tests to skip, processing the suite.opt file,
           and processing the individual test cases for data relevant
           to the rest of the test-runner
        
        """
        self.logging.verbose("Processing suite: %s" %(suite))

    def has_tests(self):
        """Return 1 if we have tests in our testlist, 0 otherwise"""
         
        return len(self.test_list)

    def get_testCase(self, requester):
        """return a testCase """
        if self.first_test:
            # we start our timer
            self.time_manager.start('total_time','total_time')
            self.first_test = 0
        test_case = None
        if self.has_tests():
            test_case = self.test_list.pop(0)
            self.record_test_executor(requester, test_case.fullname)
        return test_case

    def record_test_executor(self, requester, test_name):
        """ We record the test case and executor name as this could be useful
            We don't *know* this is needed, but we can always change this 
            later
 
        """

        self.executing_tests[test_name] = requester

    def record_test_result(self, test_case, test_status, output, exec_time):
        """ Accept the results of an executed testCase for further
            processing.
 
        """
        if test_status not in self.executed_tests:
            self.executed_tests[test_status] = [test_case]
        else:
            self.executed_tests[test_status].append(test_case)
        # report.  If the test failed, we print any additional
        # output returned by the test executor
        # We may want to report additional output at other times
        if test_status != 'pass':
            report_output = True
        else:
            report_output = False
        self.logging.test_report( test_case.fullname, test_status
                                , exec_time, output, report_output)


    def print_test_list(self):
        test_names = []
        for test in self.test_list:
            test_names.append(test.fullname)
        return "[ %s ]" %(", ".join(test_names))

    def statistical_report(self):
        """ Report out various testing statistics:
            Failed/Passed %success
            list of failed test cases
          
        """
        # This is probably hacky, but I'll think of a better
        # location later.  When we are ready to see our
        # statistical report, we know to stop the total time timer
        if not self.first_test:
            total_exec_time = self.time_manager.stop('total_time')
            self.logging.write_thick_line()
            self.logging.info("Test execution complete in %d seconds" %(total_exec_time))
        self.logging.info("Summary report:")
        self.report_executed_tests()
        self.report_test_statuses()
        if not self.first_test:
            self.time_manager.summary_report()

    def report_test_statuses(self):
        """ Method to report out various test statuses we
            care about

        """
        test_statuses = [ 'fail'
                        , 'timeout'
                        , 'skipped'
                        , 'disabled'
                        ]
        for test_status in test_statuses:
            self.report_tests_by_status(test_status)
        
    def get_executed_test_count(self):
        """ Return how many tests were executed """
        total_count = 0
        for test_list in self.executed_tests.values():
            total_count = total_count + len(test_list)
        return total_count

    def report_executed_tests(self):
        """ Report out tests by status """
        total_executed_count = self.get_executed_test_count()
        if self.total_test_count:
            executed_ratio = (float(total_executed_count)/float(self.total_test_count))
            executed_percent = executed_ratio * 100
        else:
        # We prevent division by 0 if we didn't find any tests to execute
            executed_ratio = 0
            executed_percent = 0
        self.logging.info("Executed %s/%s test cases, %.2f percent" %( total_executed_count
                                                                     , self.total_test_count
                                                                     , executed_percent))

        for test_status in self.executed_tests.keys():
            status_count = self.get_count_by_status(test_status)
            test_percent = (float(status_count)/float(total_executed_count))*100
            self.logging.info("STATUS: %s, %d/%d test cases, %.2f percent executed" %( test_status.upper()
                                                                , status_count
                                                                , total_executed_count
                                                                , test_percent 
                                                                ))


    def report_tests_by_status(self, status):
        matching_tests = []
        if status in self.executed_tests:
            for testcase in self.executed_tests[status]:
                matching_tests.append(testcase.fullname)
            self.logging.info("%s tests: %s" %(status.upper(), ", ".join(matching_tests)))

    def get_count_by_status(self, test_status):
        """ Return how many tests are in a given test_status """
        if test_status in self.executed_tests:
            return len(self.executed_tests[test_status])
        else:
            return 0

    def sort_testcases(self):
        """ Sort testcases to optimize test execution.
            This can be very mode-specific

        """
  
        self.logging.verbose("Reordering testcases to optimize test execution...")

    def has_failing_tests(self):
        return (self.get_count_by_status('fail') + self.get_count_by_status('timeout'))

