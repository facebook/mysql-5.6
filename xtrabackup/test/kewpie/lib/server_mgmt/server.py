#! /usr/bin/env python
# -*- mode: python; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
#
# Copyright (C) 2010,2011 Patrick Crews
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


""" server.py:  generic server object used by the server
    manager.  This contains the generic methods for all 
    servers.  Specific types (Drizzle, MySQL, etc) should
    inherit from this guy

"""

# imports
import os
import time
import subprocess

from lib.util.mysql_methods import execute_query

class Server(object):
    """ the server class from which other servers
        will inherit - contains generic methods
        certain methods will be overridden by more
        specific ones

    """

    def __init__(self
                , name
                , server_manager
                , code_tree
                , default_storage_engine
                , server_options
                , requester
                , test_executor = None
                , workdir_root = None):
        self.skip_keys = [ 'server_manager'
                         , 'system_manager'
                         , 'dirset'
                         , 'preferred_base_port'
                         , 'no_secure_file_priv'
                         , 'secure_file_string'
                         , 'port_block'
                         ]
        self.debug = server_manager.debug
        self.verbose = server_manager.verbose
        self.initial_run = 1
        self.timer_increment = .5
        self.owner = requester
        self.test_executor = test_executor
        self.server_options = server_options
        self.default_storage_engine = default_storage_engine
        self.server_manager = server_manager
        # We register with server_manager asap
        self.server_manager.log_server(self, requester)

        self.system_manager = self.server_manager.system_manager
        self.code_tree = code_tree
        self.version = self.code_tree.server_version
        self.type = self.code_tree.type
        self.valgrind = self.system_manager.valgrind
        self.gdb = self.system_manager.gdb
        if self.valgrind:
            self.valgrind_time_buffer = 10
        else:
            self.valgrind_time_buffer = 1
        self.cmd_prefix = self.system_manager.cmd_prefix
        self.logging = self.system_manager.logging
        self.no_secure_file_priv = self.server_manager.no_secure_file_priv
        self.name = name
        self.status = 0 # stopped, 1 = running
        self.tried_start = 0
        self.failed_test = 0 # was the last test a failure?  our state is suspect
        self.server_start_timeout = 60 * self.valgrind_time_buffer
        self.pid = None
        self.ip_address = '127.0.0.1'
        self.need_reset = False
        self.master = None
        self.need_to_set_master = False

        self.error_log = None
        self.client_init_command = None

    def initialize_databases(self):
        """ Call schemawriter to make db.opt files """
        databases = [ 'test'
                    , 'mysql'
                    ]
        for database in databases:
            db_path = os.path.join(self.datadir,'local',database,'db.opt')
            cmd = "%s %s %s" %(self.schemawriter, database, db_path)
            self.system_manager.execute_cmd(cmd)

    def process_server_options(self):
        """Consume the list of options we have been passed.
           Return a string with them joined

        """
        
        return " ".join(self.server_options)

    def take_db_snapshot(self):
        """ Take a snapshot of our vardir for quick restores """
       
        self.logging.info("Taking clean db snapshot...")
        if os.path.exists(self.snapshot_path):
            # We need to remove an existing path as python shutil
            # doesn't want an existing target
            self.system_manager.remove_dir(self.snapshot_path)
        self.system_manager.copy_dir(self.datadir, self.snapshot_path)

    def restore_snapshot(self):
        """ Restore from a stored snapshot """
        
        if not os.path.exists(self.snapshot_path):
            self.logging.error("Could not find snapshot: %s" %(self.snapshot_path))
        self.system_manager.remove_dir(self.datadir)
        self.system_manager.copy_dir(self.snapshot_path, self.datadir)
        
    def is_started(self):
        """ Is the server running?  Particulars are server-dependent """

        return "You need to implement is_started"

    def get_start_cmd(self):
        """ Return the command the server_manager can use to start me """

        return "You need to implement get_start_cmd"

    def get_stop_cmd(self):
        """ Return the command the server_manager can use to stop me """

        return "You need to implement get_stop_cmd"

    def get_ping_cmd(self):
        """ Return the command that can be used to 'ping' me 
            Very similar to is_started, but different

            Determining if a server is still running (ping)
            may differ from the method used to determine
            server startup

        """
   
        return "You need to implement get_ping_cmd"

    def set_master(self, master_server, get_cur_log_pos = True):
        """ Do what is needed to set the server to replicate
            / consider the master_server as its 'master'

        """

        return "You need to implement set_master"

    def cleanup(self):
        """ Cleanup - just free ports for now..."""
        self.system_manager.port_manager.free_ports(self.port_block)

    def set_server_options(self, server_options):
        """ We update our server_options to the new set """
        self.server_options = server_options

    def reset(self):
        """ Voodoo to reset ourselves """
        self.failed_test = 0
        self.need_reset = False

    def get_numeric_server_id(self):
        """ Return the integer value of server-id
            Mainly for mysql / percona, but may be useful elsewhere
 
        """

        return int(self.name.split(self.server_manager.server_base_name)[1])

    def start(self, working_environ=None, expect_fail=0):
        """ Start an individual server and return
            an error code if it did not start in a timely manner
 
            Start the server, using the options in option_list
            as well as self.standard_options
            
            if expect_fail = 1, we know the server shouldn't 
            start up

        """
        # set pid to None for a new start
        self.pid = None
        # get our current working environment
        if not working_environ:
            working_environ = self.test_executor.working_environment
        # take care of any environment updates we need to do
        self.server_manager.handle_environment_reqs(self, working_environ)

        self.logging.verbose("Starting server: %s.%s" %(self.owner, self.name))
        start_cmd = self.get_start_cmd()
        self.logging.debug("Starting server with:")
        self.logging.debug("%s" %(start_cmd))
        # we signal we tried to start as an attempt
        # to catch the case where a server is just 
        # starting up and the user ctrl-c's
        # we don't know the server is running (still starting up)
        # so we give it a few
        #self.tried_start = 1
        error_log = open(self.error_log,'w')
        if start_cmd: # It will be none if --manual-gdb used
            if not self.server_manager.gdb:
                server_subproc = subprocess.Popen( start_cmd
                                                 , shell=True
                                                 , env=working_environ
                                                 , stdout=error_log
                                                 , stderr=error_log
                                                 )
                server_subproc.wait()
                server_retcode = server_subproc.returncode
            else: 
                # This is a bit hackish - need to see if there is a cleaner
                # way of handling this
                # It is annoying that we have to say stdout + stderr = None
                # We might need to further manipulate things so that we 
                # have a log
                server_subproc = subprocess.Popen( start_cmd
                                                 , shell=True
                                                 , env = working_environ
                                                 , stdin=None
                                                 , stdout=None
                                                 , stderr=None
                                                 , close_fds=True
                                                 )
        
                server_retcode = 0
        else:
            # manual-gdb issue
            server_retcode = 0
        
        timer = float(0)
        timeout = float(self.server_start_timeout)

        #if server_retcode: # We know we have an error, no need to wait
        #    timer = timeout
        while not self.is_started() and timer != timeout:
            time.sleep(self.timer_increment)
            # If manual-gdb, this == None and we want to give the 
            # user all the time they need
            if start_cmd:
                timer= timer + self.timer_increment
            
        if timer == timeout and not self.ping(quiet=True):
            self.logging.error(( "Server failed to start within %d seconds.  This could be a problem with the test machine or the server itself" %(timeout)))
            server_retcode = 1
     
        if server_retcode == 0:
            self.status = 1 # we are running
            if os.path.exists(self.pid_file):
                with open(self.pid_file,'r') as pid_file:
                    pid = pid_file.readline().strip()
                    pid_file.close()
                self.pid = pid

        if server_retcode != 0 and not expect_fail:
            self.logging.error("Server startup command: %s failed with error code %d" %( start_cmd
                                                                                  , server_retcode))
            self.logging.error("Dumping error log: %s" %(self.error_log))
            with open(self.error_log,'r') as errlog:
                for line in errlog:
                    self.logging.error(line.strip())
        elif server_retcode == 0 and expect_fail:
        # catch a startup that should have failed and report
            self.logging.error("Server startup command :%s expected to fail, but succeeded" %(start_cmd))

        self.tried_start = 0 
        if self.need_to_set_master:
            # TODO handle a bad slave retcode
            slave_retcode = self.set_master(self.master)
        return server_retcode ^ expect_fail

    def ping(self, quiet=False):
        """ Ping / check if the server is alive 
            Return True if server is up and running, False otherwise
        """
 
        ping_cmd = self.get_ping_cmd()
        if not quiet:
            self.logging.info("Pinging %s server on port %d" % (self.type.upper(), self.master_port))
        (retcode, output)= self.system_manager.execute_cmd(ping_cmd, must_pass = 0)
        return retcode == 0
             

    def stop(self):
        """ Stop an individual server if it is running """
        if self.tried_start:
            # we expect that we issued the command to start
            # the server but it isn't up and running
            # we kill a bit of time waiting for it
            attempts_remain = 10
            while not self.ping(quiet=True) and attempts_remain:
                time.sleep(1)
                attempts_remain = attempts_remain - 1
        # Now we try to shut the server down
        if self.ping(quiet=True):
            self.logging.verbose("Stopping server %s.%s" %(self.owner, self.name))
            stop_cmd = self.get_stop_cmd()
            self.logging.debug("with shutdown command:\n %s" %(stop_cmd))
            #retcode, output = self.system_manager.execute_cmd(stop_cmd)
            shutdown_subproc = subprocess.Popen( stop_cmd
                                               , shell=True
                                               )
            shutdown_subproc.wait()
            shutdown_retcode = shutdown_subproc.returncode
            # We do some monitoring for the server PID and kill it
            # if need be.  This is a bit of a band-aid for the 
            # zombie-server bug on Natty : (  Need to find the cause.
            attempts_remain = 100
            while self.system_manager.find_pid(self.pid) and attempts_remain:
                time.sleep(1)
                attempts_remain = attempts_remain - 1
                if not attempts_remain: # we kill the pid
                    if self.verbose:
                        self.logging.warning("Forcing kill of server pid: %s" %(server.pid))
                    self.system_manager.kill_pid(self.pid)
            if shutdown_retcode:
                self.logging.error("Problem shutting down server:")
                self.logging.error("%s" %(shutdown_retcode))
                self.status = 0
            else:
                self.status = 0 # indicate we are shutdown
        else:
            # make sure the server is indicated as stopped
            self.status = 0

    def die(self):
        """ This causes us to kill the server pid """
        self.system_manager.kill_pid(self.get_pid())

    def get_pid(self):
        """ We check our pid file and get what is there """
        if os.path.exists(self.pid_file):
            with open(self.pid_file,'r') as pid_file:
                pid = pid_file.readline().strip()
                pid_file.close()
            self.pid = pid
            return self.pid

    def get_engine_info(self):
            """ Check innodb / xtradb version """

            innodb_version = None
            xtradb_version = None
        #if not self.code_tree.version_checked:
            query = "SHOW VARIABLES LIKE 'innodb_version'"
            retcode, result = execute_query(query, self)
            # result format = (('innodb_version', '1.1.6-20.1'),)
            if result:
                innodb_version = result[0][1]
                split_data = innodb_version.split('-')
                if len(split_data) > 1:
                    xtradb_version = split_data[-1]
            self.code_tree.version_checked = True
            self.code_tree.innodb_version = innodb_version
            self.code_tree.xtradb_version = xtradb_version 
            return innodb_version, xtradb_version

    def dump_errlog(self):
        with open(self.error_log,'r') as errlog:
            data = errlog.readlines()
        return ''.join(data) 
 
