#! /usr/bin/env python
# -*- mode: python; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
#
# Copyright (C) 2010 Patrick Crews
#
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

""" sysbench_test_execution:
    code related to the execution of sysbench test cases 
    
    We are provided access to a testManager with 
    sysbench-specific testCases.  

"""

# imports
import os
import re
import sys
import subprocess
import commands

import lib.test_mgmt.test_execution as test_execution

class testExecutor(test_execution.testExecutor):
    """ sysbench-specific testExecutor 
        
    """
  
    def execute_testCase (self):
        """ Execute a sysbench testCase

        """
        test_execution.testExecutor.execute_testCase(self)
        self.status = 0

        # prepare the server for sysbench
        self.prepare_sysbench()

        # execute sysbench
        self.execute_sysbench()

        # analyze results
        self.current_test_status = self.process_sysbench_output()
        self.set_server_status(self.current_test_status)
        self.server_manager.reset_servers(self.name)
 
    def prepare_sysbench(self):
        """ Prepare the server for a sysbench run
            We use subprocess as we can pass os.environ dicts and whatnot 

        """
      
        sysbench_outfile = os.path.join(self.logdir,'sysbench.out')
        sysbench_output = open(sysbench_outfile,'w')
        sysbench_cmd = ' '.join([self.current_testcase.test_command,'prepare'])      
        self.logging.info("Preparing database for sysbench run...")
        self.logging.debug(sysbench_cmd)
        sysbench_subproc = subprocess.Popen( sysbench_cmd
                                         , shell=True
                                         #, cwd=os.getcwd()
                                         , env=self.working_environment
                                         , stdout = sysbench_output
                                         , stderr = subprocess.STDOUT
                                         )
        sysbench_subproc.wait()
        retcode = sysbench_subproc.returncode

        sysbench_output.close()
        sysbench_file = open(sysbench_outfile,'r')
        output = ''.join(sysbench_file.readlines())
        sysbench_file.close()
        self.logging.debug("sysbench_retcode: %d" %(retcode))
        self.logging.debug(output)
        if retcode:
            self.logging.error("sysbench_prepare failed with retcode %d:" %(retcode))
            self.logging.error(output)   
            sys.exit(1)
            

    

    def execute_sysbench(self):
        """ Execute the commandline and return the result.
            We use subprocess as we can pass os.environ dicts and whatnot 

        """
      
        testcase_name = self.current_testcase.fullname
        self.time_manager.start(testcase_name,'test')
        sysbench_outfile = os.path.join(self.logdir,'sysbench.out')
        sysbench_output = open(sysbench_outfile,'w')
        sysbench_cmd = ' '.join([self.current_testcase.test_command, 'run'])
        self.logging.info("Executing sysbench:  %s" %(sysbench_cmd))
        
        sysbench_subproc = subprocess.Popen( sysbench_cmd
                                         , shell=True
                                         #, cwd=self.system_manager.sysbench_path
                                         , env=self.working_environment
                                         , stdout = sysbench_output
                                         , stderr = subprocess.STDOUT
                                         )
        sysbench_subproc.wait()
        retcode = sysbench_subproc.returncode     
        execution_time = int(self.time_manager.stop(testcase_name)*1000) # millisec

        sysbench_output.close()
        sysbench_file = open(sysbench_outfile,'r')
        output = ''.join(sysbench_file.readlines())
        self.logging.debug(output)
        sysbench_file.close()

        self.logging.debug("sysbench_retcode: %d" %(retcode))
        self.current_test_retcode = retcode
        self.current_test_output = output
        self.current_test_exec_time = execution_time

    def process_sysbench_output(self):
        """ sysbench has run, we now check out what we have 
            We also output the data from the run
        
        """
        # This slice code taken from drizzle-automation's sysbench handling
        # Slice up the output report into a matrix and insert into the DB.
        regexes= {
          'tps': re.compile(r".*transactions\:\s+\d+\D*(\d+\.\d+).*")
        , 'deadlocksps': re.compile(r".*deadlocks\:\s+\d+\D*(\d+\.\d+).*")
        , 'rwreqps': re.compile(r".*read\/write\s+requests\:\s+\d+\D*(\d+\.\d+).*")
        , 'min_req_lat_ms': re.compile(r".*min\:\s+(\d*\.\d+)ms.*")
        , 'max_req_lat_ms': re.compile(r".*max\:\s+(\d*\.\d+)ms.*")
        , 'avg_req_lat_ms': re.compile(r".*avg\:\s+(\d*\.\d+)ms.*")
        , '95p_req_lat_ms': re.compile(r".*approx.\s+95\s+percentile\:\s+(\d+\.\d+)ms.*")
        }
        run= {}
        for line in self.current_test_output.split("\n"):
            for key in regexes.keys():
                result= regexes[key].match(line)
                if result:
                    run[key]= float(result.group(1)) # group(0) is entire match...
        # we set our test output to the regex'd-up data
        # we also make it a single string, separated by newlines
        self.current_test_output = str(run)[1:-1].replace(',','\n').replace("'",'')
                    
        if self.current_test_retcode == 0:
            return 'pass'
        else:
            return 'fail'

