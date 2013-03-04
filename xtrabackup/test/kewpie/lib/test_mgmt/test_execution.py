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

""" test_execution:
    code related to the execution of test cases 
    
    We are provided access to a testManager with 
    mode-specific testCases.  We contact the executionManager
    to produce the system and server configurations we need
    to execute a test.

"""

# imports
import os
import sys
import subprocess

class testExecutor():
    """ class for handling the execution of testCase
        objects.  Mode-specific executors inherit
        from me.
 
    """

    def __init__(self, execution_manager, name, verbose, debug):
        self.skip_keys = [ 'execution_manager'
                         , 'system_manager'
                         , 'test_manager'
                         , 'server_manager']
        self.debug = debug
        self.verbose = verbose
        self.initial_run = 1
        self.status = 0 # not running
        self.execution_manager = execution_manager
        self.system_manager = self.execution_manager.system_manager
        self.testcase_repeat_count = self.execution_manager.testcase_repeat_count
        self.cmd_prefix = self.system_manager.cmd_prefix
        self.logging = self.system_manager.logging
        self.test_manager = self.execution_manager.test_manager
        self.server_manager = self.execution_manager.server_manager
        self.time_manager = self.system_manager.time_manager
        self.matrix_manager = self.execution_manager.matrix_manager
        self.name = name
        self.working_environment = {} # we pass env dict to define what we need
        self.dirset = { self.name : { 'log': None } }
        self.workdir = self.system_manager.create_dirset( self.system_manager.workdir
                                                        , self.dirset)
        self.logdir = os.path.join(self.workdir,'log')
        self.master_server = self.server_manager.allocate_server( self.name
                                                                , self
                                                                , []
                                                                , self.workdir
                                                                )
        self.record_flag=self.execution_manager.record_flag
        self.current_servers = [self.master_server]
        self.current_testcase = None    
        self.current_test_status = None
        self.current_test_retcode = None
        self.current_test_output = None
        self.current_test_exec_time = 0 
         
        self.logging.debug_class(self)

    def execute(self, start_and_exit):
        """ Execute a test case.  The details are *very* mode specific """
        self.status = 1 # we are running
        keep_running = 1
        self.logging.verbose("Executor: %s beginning test execution..." %(self.name))
        while self.test_manager.has_tests() and keep_running == 1:
            self.get_testCase()
            for i in range(self.testcase_repeat_count):
                if keep_running:
                    if self.current_testcase.should_run():
                        self.handle_system_reqs()
                        self.handle_server_reqs()
                        self.handle_utility_reqs()
                        self.handle_start_and_exit(start_and_exit)
                        if self.current_test_status != 'fail':
                            self.execute_testCase()
                    else:
                        # the test's skip_flag is set or it is disabled
                        self.current_test_status = 'skipped'
                        self.current_test_output = self.current_testcase.skip_reason
                    # Warn the user if the test suddenly starts passing
                    if self.current_testcase.expect_fail and self.current_test_status != fail:
                        self.logging.warning("Test: %s was expected to fail but has passed!" %self.current_testcase.fullname)
                    self.record_test_result()
                    if self.current_test_status == 'fail' and not self.execution_manager.force and not self.current_testcase.expect_fail:
                        self.logging.error("Failed test.  Use --force to execute beyond the first test failure")
                        keep_running = 0
                    self.current_test_status = None # reset ourselves
                    
        self.status = 0

    def get_testCase(self):
        """ Ask our execution_manager for a testCase to work on """
        
        #self.test_manager.mutex.acquire()
        self.current_testcase = self.test_manager.get_testCase(self.name)
        #self.test_manager.mutex.release()
        

    def handle_server_reqs(self):
        """ Get the servers required to execute the testCase 
            and ensure that we have servers and they were started
            as expected.  We take necessary steps if not
            We also handle --start-and-exit here
 
        """

        server_requirements = self.current_testcase.server_requirements
        if server_requirements:
            (self.current_servers,bad_start) = self.server_manager.request_servers( self.name
                                                                  , self.workdir
                                                                  , self.current_testcase.cnf_path
                                                                  , self.current_testcase.server_requests
                                                                  , server_requirements
                                                                  , self)
            if self.current_servers == 0 or bad_start:
                # error allocating servers, test is a failure
                self.logging.warning("Problem starting server(s) for test...failing test case")
                self.current_test_status = 'fail'
                self.set_server_status(self.current_test_status)
                output = ''           
            if self.initial_run:
                self.initial_run = 0
                self.current_servers[0].report()
            self.master_server = self.current_servers[0]
            if len(self.current_servers) > 1:
                # We have a validation server or something we need to communicate with
                # We export some env vars with EXECUTOR_SERVER and expect the randge
                # code to know enough to look for this marker
                extra_reqs = {}
                for server in self.current_servers:
                    variable_name = "%s_%s" %(self.name.upper(), server.name.upper())
                    variable_value = str(server.master_port)
                    extra_reqs[variable_name] = variable_value
                    variable_name = variable_name + "_PID"
                    variable_value = str(server.pid)
                    extra_reqs[variable_name] = variable_value
                self.working_environment.update(extra_reqs)
            return 

    def handle_start_and_exit(self, start_and_exit):
        """ Do what needs to be done if we have the
            --start-and-exit flag

        """
        if start_and_exit:
                # We blow away any port_management files for our ports
                # Technically this won't let us 'lock' any ports that 
                # we aren't explicitly using (visible to netstat scan)
                # However one could argue that if we aren't using it, 
                # We shouldn't hog it ; )
                # We might need to do this better later
                for server in self.current_servers:
                    if server != self.master_server:
                        server.report()
                    server.cleanup() # this only removes any port files
                self.logging.info("User specified --start-and-exit.  kewpie.py exiting and leaving servers running...") 
                sys.exit(0)

    def handle_utility_reqs(self):
        """ Call any utilities we want to use before starting a test
            At present this is envisioned for use with datagen
            but there may be other things we wish to use
            At that point, we may need to explore other ways of
            defining our testing environment, such as with
            nice config files / modules

        """

        # We call gendata against the server(s) with the
        # specified file
        if self.execution_manager.gendata_file:
            dsn = "--dsn=dbi:%s:host=127.0.0.1:port=%d:user=root:password="":database=test" %( self.master_server.type
                                                                                             , self.master_server.master_port)
            gendata_cmd = "./gendata.pl %s --spec=%s" %( dsn 
                                                       , self.execution_manager.gendata_file
                                                       )
            #self.system_manager.execute_cmd(gendata_cmd)
            gendata_subproc = subprocess.Popen( gendata_cmd
                                              , shell=True
                                              , cwd=self.system_manager.randgen_path
                                              , stdout = None
                                              , stderr = None
                                              )
            gendata_subproc.wait()
            gendata_retcode = gendata_subproc.returncode
            if gendata_retcode:
                self.logging.error("gendata command: %s failed with retcode: %d" %(gendata_cmd
                                                                             , gendata_retcode))

    def execute_testCase(self):
        """ Do whatever evil voodoo we must do to execute a testCase """
        self.logging.verbose("Executor: %s executing test: %s" %(self.name, self.current_testcase.fullname))

    def record_test_result(self):
        """ We get the test_manager to record the result """

        self.test_manager.record_test_result( self.current_testcase
                                                , self.current_test_status
                                                , self.current_test_output
                                                , self.current_test_exec_time )

            
    def set_server_status(self, test_status):
        """ We update our servers to indicate if a test passed or failed """
        for server in self.current_servers:
            if test_status == 'fail':
                server.failed_test = 1

   
    def handle_system_reqs(self):
        """ We check our test case and see what we need to do
            system-wise to get ready.  This is likely to be 
            mode-dependent and this is just a placeholder
            method

        """

        self.process_environment_reqs()
        self.process_symlink_reqs()
        self.process_master_sh()  
        return

    def process_master_sh(self):
        """ We do what we need to if we have a master.sh file """
        if self.current_testcase.master_sh:
            retcode, output = self.system_manager.execute_cmd("/bin/sh %s" %(self.current_testcase.master_sh))
            self.logging.debug("retcode: %retcode")
            self.logging.debug("%output")

    def process_environment_reqs(self):
        """ We generate the ENV vars we need set
            and then ask systemManager to do so

        """
        # We need to have a default set / file / whatever based
        # on the dbqp config file / what we're using dbqp for
        # will move this dict elsewhere to make this method more generic
        #
        # We're also doing the kludgy thing of basing env_reqs on 
        # the master_server type.  At some point, we'll see about making
        # this more flexible / extensible.

        if self.master_server.type == 'drizzle':
            env_reqs = { 'DRIZZLETEST_VARDIR': self.master_server.vardir
                       ,  'DRIZZLE_TMP_DIR': self.master_server.tmpdir
                       ,  'MASTER_MYSOCK': self.master_server.socket_file
                       ,  'MASTER_MYPORT': str(self.master_server.master_port)
                       ,  'MC_PORT': str(self.master_server.mc_port)
                       ,  'PBMS_PORT': str(self.master_server.pbms_port)
                       ,  'JSON_SERVER_PORT': str(self.master_server.json_server_port)
                       ,  'RABBITMQ_NODE_PORT': str(self.master_server.rabbitmq_node_port)
                       ,  'DRIZZLE_TCP_PORT': str(self.master_server.drizzle_tcp_port)
                       ,  'EXE_DRIZZLE': self.master_server.drizzle_client
                       ,  'MASTER_SERVER_SLAVE_CONFIG' : self.master_server.slave_config_file
                       ,  'DRIZZLE_DUMP': "%s --no-defaults -uroot -p%d" %( self.master_server.drizzledump
                                                            , self.master_server.master_port)
                       ,  'DRIZZLE_SLAP': "%s -uroot -p%d" %( self.master_server.drizzleslap
                                                            , self.master_server.master_port)
                       ,  'DRIZZLE_IMPORT': "%s -uroot -p%d" %( self.master_server.drizzleimport
                                                              , self.master_server.master_port)
                       ,  'DRIZZLE': "%s -uroot -p%d" %( self.master_server.drizzle_client
                                                       , self.master_server.master_port)
                       ,  'DRIZZLE_BASEDIR' : self.system_manager.code_manager.code_trees['drizzle'][0].basedir
                       ,  'DRIZZLE_TRX_READER' : self.master_server.trx_reader
                       ,  'DRIZZLE_TEST_WORKDIR' : self.system_manager.workdir
                       ,  'SQLBENCH_DIR' : os.path.join( self.system_manager.testdir
                                                       , 'sql-bench')
                       }
        elif self.master_server.type in ['mysql','percona','galera']:
            env_reqs = {  'MYSQLTEST_VARDIR': self.master_server.vardir
                       ,  'MYSQL_TMP_DIR': self.master_server.tmpdir
                       ,  'MASTER_MYSOCK': self.master_server.socket_file
                       ,  'MASTER_MYPORT': str(self.master_server.master_port)
                       ,  'EXE_MYSQL': self.master_server.mysql_client
                       ,  'MYSQL_DUMP': "%s --no-defaults -uroot -p%d" %( self.master_server.mysqldump
                                                            , self.master_server.master_port)
                       ,  'MYSQL_SLAP': "%s -uroot -p%d" %( self.master_server.mysqlslap
                                                          , self.master_server.master_port)
                       ,  'MYSQL_IMPORT': "%s -uroot -p%d" %( self.master_server.mysqlimport
                                                            , self.master_server.master_port)
                       ,  'MYSQL_UPGRADE': "%s -uroot --datadir=%s" %( self.master_server.mysql_upgrade
                                                                     , self.master_server.datadir)
                       ,  'MYSQL': "%s -uroot -p%d" %( self.master_server.mysql_client
                                                     , self.master_server.master_port)
                       #,  'MYSQL_BASEDIR' : self.system_manager.code_manager.code_trees['mysql'][0].basedir
                       ,  'MYSQL_TEST_WORKDIR' : self.system_manager.workdir
                       ,  'SQLBENCH_DIR' : os.path.join( self.system_manager.testdir
                                                       , 'sql-bench')
                       }         

        self.working_environment = self.system_manager.env_manager.create_working_environment(env_reqs)

    def process_symlink_reqs(self):
        """ Create any symlinks we may need """
        needed_symlinks = []

        self.system_manager.create_symlinks(needed_symlinks)

    
   


   
