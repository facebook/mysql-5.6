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

"""execution_management.py
   code for dealing with test execution.
   The actual nuts and bolts of executing a test lies with the 
   mode-specific test-executor

   The code here is for managing the executors and such.

"""

# imports
import thread
import time

class executionManager:
    """ Manages the mode-specific test-executors
        and serves as an intermediary between the executors
        and the other management code (system, server, test)

    """

    def __init__(self, server_manager, system_manager, test_manager
                 , executor_type, variables, matrix_manager):

        self.server_manager = server_manager
        self.system_manager = system_manager
        self.matrix_manager = matrix_manager
        self.logging = system_manager.logging
        self.test_manager = test_manager
        if variables['verbose']:
            self.logging.info("Initializing test execution manager...")
        self.skip_keys = [ 'server_manager'
                         , 'system_manager'
                         , 'test_manager'
                         , 'executors'
                         , 'executor_start_count'
                         , 'executor_current_count'
                         , 'record_flag'
                         , 'start_and_exit'
                         ]
        
        self.debug = variables['debug']
        self.verbose = variables['verbose']
        self.force = variables['force']
        self.record_flag = variables['record']
        self.gendata_file = variables['gendatafile']
        self.testcase_repeat_count = variables['repeat']
        # We are currently single-threaded execution-wise
        # but in the future, we will likely need to revamp
        # how we deal with start-and-exit if we have multiple
        # executors - even if we force-set the executor-count
        # to 1.
        self.start_and_exit = variables['startandexit']
        self.executors = {}
        self.executor_name_base = 'bot'
        self.executor_start_count = 0
        self.executor_current_count = 0
        # We will eventually allow this to be set
        # by the user, but we are hard-coded at 1
        # for the moment (will = --parallel)
        self.executor_count = 1 
        self.executor_type = executor_type

        self.logging.debug_class(self)


    def execute_tests(self):
        """ Execute the testCases stored in the testManager
            via spawning executor_count of the mode-specific
            executor_types.
 
            Currently only supporting single executor, but trying
            to plan ahead

        """
        
        # create our testExecutors to execute the tests
        self.create_test_executors()

        # fire up the testExecutors and let them rip it up
        for executor_name, executor in self.executors.items():
            self.logging.verbose("Starting executor: %s" %(executor_name))
            # thread.start_new(executor.execute,()) # sigh...one day...damned drizzletest!
            executor.execute(self.start_and_exit)
        while self.has_running_executors():
            pass
        self.test_manager.statistical_report()
        self.logging.info("Test execution complete")

    def has_running_executors(self):
        """ We see if our executors are still running """
        for executor in self.executors.values():
            if executor.status == 1:
                return 1
        return 0

    def create_test_executors(self):
        """ create however many test executors of executor_type
            that we need (only supporting 1 for now)

        """

        self.logging.info("Creating %d %s(s)" %(self.executor_count
                                          , self.executor_name_base))
        for i in range(self.executor_start_count,self.executor_count,1):
            executor_name = "%s%d" %(self.executor_name_base,i)
            new_executor = self.create_test_executor(executor_name)
           

    def create_test_executor(self, executor_name):
        """ Create a single testExecutor """
        
        self.logging.verbose("Creating %s" %(executor_name))
        new_executor = self.executor_type( self, executor_name
                                         , self.verbose, self.debug)
        self.log_executor(executor_name, new_executor)

    def log_executor(self, executor_name, new_executor):
        """ Bookkeeping function to track created executors """
        self.executors[executor_name] = new_executor
        self.executor_current_count += 1

  
