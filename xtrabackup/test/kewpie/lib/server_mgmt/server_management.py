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

"""server_management.py
   code for dealing with apportioning servers
   to suit the needs of the tests and executors

"""
# imports
import time
import os
import shutil
import subprocess
from ConfigParser import RawConfigParser

class serverManager:
    """ code that handles the server objects
        We use this to track, do mass actions, etc
        Single point of contact for this business

    """

    def __init__(self, system_manager, variables):
        self.skip_keys = [ 'system_manager'
                         , 'env_manager'
                         , 'code_manager'
                         , 'logging'
                         , 'gdb'
                         ]
        self.debug = variables['debug']
        self.verbose = variables['verbose']
        self.initial_run = 1
        # we try this to shorten things - will see how this works
        self.server_base_name = 's'
        self.no_secure_file_priv = variables['nosecurefilepriv']
        self.system_manager = system_manager
        self.code_manager = system_manager.code_manager
        self.env_manager = system_manager.env_manager
        self.logging = system_manager.logging
        self.gdb  = self.system_manager.gdb
        self.default_storage_engine = variables['defaultengine']
        self.default_server_type = variables['defaultservertype']
        self.user_server_opts = variables['drizzledoptions']
        self.servers = {}

        self.libeatmydata = variables['libeatmydata']
        self.libeatmydata_path = variables['libeatmydatapath']

        self.logging.info("Using default-storage-engine: %s" %(self.default_storage_engine))
        test_server = self.allocate_server( 'test_bot' 
                                          , None 
                                          , []
                                          , self.system_manager.workdir
                                          )
        self.logging.info("Testing for Innodb / Xtradb version...")
        test_server.start(working_environ=os.environ)
        try:
            innodb_ver, xtradb_ver = test_server.get_engine_info()
            self.logging.info("Innodb version: %s" %innodb_ver)
            self.logging.info("Xtradb version: %s" %xtradb_ver)
        except Exception, e:
            self.logging.error("Problem detecting innodb/xtradb version:")
            self.logging.error(Exception)
            self.logging.error(e)
            self.logging.error("Dumping server error.log...")
            test_server.dump_errlog()
        test_server.stop()
        test_server.cleanup()
        shutil.rmtree(test_server.workdir)
        del(test_server)

        self.logging.debug_class(self)

    def request_servers( self
                       , requester 
                       , workdir
                       , cnf_path
                       , server_requests
                       , server_requirements
                       , test_executor
                       , expect_fail = 0):
        """ We produce the server objects / start the server processes
            as requested.  We report errors and whatnot if we can't
            That is, unless we expect the server to not start, then
            we just return a value / message.

            server_requirements is a list of lists.  Each list
            is a set of server options - we create one server
            for each set of options requested
    
        """

        # Make sure our server is in a decent state, if the last test
        # failed, then we reset the server
        self.check_server_status(requester)
        
        # Make sure we have the proper number of servers for this requester
        self.process_server_count( requester
                                 , test_executor
                                 , len(server_requirements)
                                 , workdir
                                 , server_requirements)

        # Make sure we are running with the correct options 
        self.evaluate_existing_servers( requester
                                      , cnf_path
                                      , server_requests
                                      , server_requirements)

        # Fire our servers up
        bad_start = self.start_servers( requester
                                      , expect_fail)
 
        # Return them to the requester
        return (self.get_server_list(requester), bad_start)        


    
    def allocate_server( self
                       , requester
                       , test_executor
                       , server_options
                       , workdir
                       , server_type=None
                       , server_version=None):
        """ Intialize an appropriate server object.
            Start up occurs elsewhere

        """
        # use default server type unless specifically requested to do otherwise
        if not server_type:
            server_type = self.default_server_type

        # Get a name for our server
        server_name = self.get_server_name(requester)

        # initialize our new server_object
        # get the right codeTree type from the code manager
        code_tree = self.code_manager.get_tree(server_type, server_version)

        # import the correct server type object
        if server_type == 'drizzle':
            from lib.server_mgmt.drizzled import drizzleServer as server_type
        elif server_type == 'mysql':
            from lib.server_mgmt.mysqld import mysqlServer as server_type
        elif server_type == 'galera':
            from lib.server_mgmt.galera import mysqlServer as server_type

        new_server = server_type( server_name
                                , self
                                , code_tree
                                , self.default_storage_engine
                                , server_options
                                , requester
                                , test_executor
                                , workdir )
        return new_server

    def start_servers(self, requester, expect_fail):
        """ Start all servers for the requester """
        bad_start = 0

        for server in self.get_server_list(requester):
            if server.status == 0:
                bad_start = bad_start + server.start()
            else:
                self.logging.debug("Server %s already running" %(server.name))
        return bad_start

    def stop_servers(self, requester):
        """ Stop all servers running for the requester """
        for server in self.get_server_list(requester):
            server.stop()

    def stop_server_list(self, server_list, free_ports=False):
        """ Stop the servers in an arbitrary list of them """
        for server in server_list:
            server.stop()
        if free_ports:
            server.cleanup()

    def stop_all_servers(self):
        """ Stop all running servers """

        self.logging.info("Stopping all running servers...")
        for server_list in self.servers.values():
            for server in server_list:
                server.stop()

    def cleanup_all_servers(self):
        """Mainly for freeing server ports for now """
        for server_list in self.servers.values():
            for server in server_list:
                server.cleanup()

    def cleanup(self):
        """Stop all servers and free their ports and whatnot """
        self.stop_all_servers()
        self.cleanup_all_servers()

    def get_server_name(self, requester):
        """ We name our servers requester.server_basename.count
            where count is on a per-requester basis
            We see how many servers this requester has and name things 
            appropriately

        """
        self.has_servers(requester) # if requester isn't there, we create a blank entry
        server_count = self.server_count(requester)
        return "%s%d" %(self.server_base_name, server_count)

    def has_servers(self, requester):
        """ Check if the given requester has any servers """
        if requester not in self.servers: # new requester
           self.log_requester(requester) 
        return self.server_count(requester)

    def log_requester(self, requester):
        """ We create a log entry for the new requester """

        self.servers[requester] = []

    def log_server(self, new_server, requester):
        self.servers[requester].append(new_server)

    def evaluate_existing_servers( self, requester, cnf_path
                                 , server_requests, server_requirements):
        """ See if the requester has any servers and if they
            are suitable for the current test

            We should have the proper number of servers at this point

        """

        # A dictionary that holds various tricks
        # we can do with our test servers
        special_processing_reqs = {}
        if server_requests:
            # we have a direct dictionary in the testcase
            # that asks for what we want and we use it
            special_processing_reqs = server_requests

        current_servers = self.servers[requester]

        for index,server in enumerate(current_servers):
            # We handle a reset in case we need it:
            if server.need_reset:
                self.reset_server(server)
                server.need_reset = False

            desired_server_options = server_requirements[index]
            
            # do any special config processing - this can alter
            # how we view our servers
            if cnf_path:
                self.handle_server_config_file( cnf_path
                                              , server
                                              , special_processing_reqs
                                              , desired_server_options
                                              )

            if self.compare_options( server.server_options
                                   , desired_server_options):
                pass 
            else:
                # We need to reset what is running and change the server
                # options
                desired_server_options = self.filter_server_options(desired_server_options)
                self.reset_server(server)
                self.update_server_options(server, desired_server_options)
        self.handle_special_server_requests(special_processing_reqs, current_servers)

    def handle_server_config_file( self
                                 , cnf_path
                                 , server
                                 , special_processing_reqs
                                 , desired_server_options
                                 ):
        # We have a config reader so we can do
        # special per-server magic for setting up more
        # complex scenario-based testing (eg we use a certain datadir)
        config_reader = RawConfigParser()
        config_reader.read(cnf_path)

        # Do our checking for config-specific madness we need to do
        if config_reader and config_reader.has_section(server.name):
            # mark server for restart in case it hasn't yet
            # this method is a bit hackish - need better method later
            if '--restart' not in desired_server_options:
                desired_server_options.append('--restart')
            # We handle various scenarios
            server_config_data = config_reader.items(server.name)
            for cnf_option, data in server_config_data:
                if cnf_option == 'load-datadir':
                    datadir_path = data
                    request_key = 'datadir_requests'
                if request_key not in special_processing_reqs:
                    special_processing_reqs[request_key] = []
                special_processing_reqs[request_key].append((datadir_path,server))

    def handle_special_server_requests(self, request_dictionary, current_servers):
        """ We run through our set of special requests and do 
            the appropriate voodoo

        """
        for key, item in request_dictionary.items():
            if key == 'datadir_requests':
                self.load_datadirs(item, current_servers)
            if key == 'join_cluster':
                self.join_clusters(item, current_servers)

    def filter_server_options(self, server_options):
        """ Remove a list of options we don't want passed to the server
            these are test-case specific options.
 
            NOTE: It is a bad hack to allow test-runner commands
            to mix with server options willy-nilly in master-opt files
            as we do.  We need to kill this at some point : (

        """
        remove_options = [ '--restart'
                         , '--skip-stack-trace'
                         , '--skip-core-file'
                         , '--'
                         ]
        for remove_option in remove_options:
            if remove_option in server_options:
                server_options.remove(remove_option)
        return server_options
            
    
    def compare_options(self, optlist1, optlist2):
        """ Compare two sets of server options and see if they match """
        return sorted(optlist1) == sorted(optlist2)

    def reset_server(self, server):
        server.stop()
        server.restore_snapshot()
        server.reset()

    def reset_servers(self, requester):
        for server in self.servers[requester]:
            self.reset_server(server)

    def load_datadirs(self, datadir_requests, current_servers):
        """ We load source_dir to the server's datadir """
        for source_dir, server in datadir_requests:
            self.load_datadir(source_dir, server, current_servers)

    def load_datadir(self, source_dir, server, current_servers):
        """ We load source_dir to the server's datadir """

        if type(server) == int:
            # we have just an index (as we use in unittest files)
            # and we get the server from current_servers[idx]
            server = current_servers[server]
        source_dir_path = os.path.join(server.vardir,'std_data_ln',source_dir)
        self.system_manager.remove_dir(server.datadir)
        self.system_manager.copy_dir(source_dir_path, server.datadir)
        # We need to signal that the server will need to be reset as we're
        # using a non-standard datadir
        server.need_reset = True

    def join_clusters(self, cluster_requests, current_servers):
        """ We get a list of master, slave tuples and join
            them as needed

        """
        for cluster_set in cluster_requests:
            self.join_node_to_cluster(cluster_set, current_servers)
        

    def join_node_to_cluster(self, node_set, current_servers):
        """ We join node_set[1] to node_set[0].
            The server object is responsible for 
            implementing the voodoo required to 
            make this happen

        """
            
        master = current_servers[node_set[0]]
        slave = current_servers[node_set[1]]
        slave.set_master(master)
        # Assuming we'll reset master and slave for now...
        master.need_reset = True
        slave.need_reset = True

    def process_server_count( self
                            , requester
                            , test_executor
                            , desired_count
                            , workdir
                            , server_reqs):
        """ We see how many servers we have.  We shrink / grow
            the requesters set of servers as needed.

            If we shrink, we shutdown / reset the discarded servers
            (naturally)
 
        """
        if desired_count < 0:  desired_count = 1

        current_count = self.has_servers(requester)
        if desired_count > current_count:
            for i in range(desired_count - current_count):
                # We pass an empty options list when allocating
                # We'll update the options to what is needed elsewhere
                self.allocate_server(requester, test_executor, [], workdir)
        elif desired_count < current_count:
            good_servers = self.get_server_list(requester)[:desired_count]
            retired_servers = self.get_server_list(requester)[desired_count - current_count:]
            self.stop_server_list(retired_servers, free_ports=True)
            self.set_server_list(requester, good_servers)
            
         

    def server_count(self, requester):
        """ Return how many servers the the requester has """
        return len(self.servers[requester])

    def get_server_list(self, requester):
        """ Return the list of servers assigned to the requester """
        self.has_servers(requester) # initialize, hacky : (
        return self.servers[requester]
 
    def set_server_list(self, requester, server_list):
        """ Set the requesters list of servers to server_list """

        self.servers[requester] = server_list

    def add_server(self, requester, new_server):
       """ Add new_server to the requester's set of servers """
       self.servers[requester].append(new_server)

    def update_server_options(self, server, server_options):
        """ Change the option_list a server has to use on startup """
        self.logging.debug("Updating server: %s options" %(server.name))
        self.logging.debug("FROM: %s" %(server.server_options))
        self.logging.debug("TO: %s" %(server_options))
        server.set_server_options(server_options)

    def get_server_count(self):
        """ Find out how many servers we have out """
        server_count = 0
        for server_list in self.servers.values():
            for server in server_list:
                server_count = server_count + 1
        return server_count

    def check_server_status(self, requester):
        """ Make sure our servers are good,
            reset the otherwise.

        """
        for server in self.get_server_list(requester):
            if server.failed_test:
                self.reset_server(server)

    def handle_environment_reqs(self, server, working_environ):
        """ We update the working_environ as we need to
            before starting the server.

            This includes things like libeatmydata, ld_preloads, etc

        """
        environment_reqs = {}

        if self.libeatmydata:
            # We want to use libeatmydata to disable fsyncs
            # this speeds up test execution, but we only want
            # it to happen for the servers' environments

            environment_reqs.update({'LD_PRELOAD':self.libeatmydata_path})

        # handle ld_preloads
        ld_lib_paths = self.env_manager.join_env_var_values(server.code_tree.ld_lib_paths)
        environment_reqs.update({'LD_LIBRARY_PATH' : self.env_manager.append_env_var( 'LD_LIBRARY_PATH'
                                                                                    , ld_lib_paths
                                                                                    , suffix = 0
                                                                                    , quiet = 1
                                                                                    )
                                , 'DYLD_LIBRARY_PATH' : self.env_manager.append_env_var( 'DYLD_LIBRARY_PATH'
                                                                                       , ld_lib_paths
                                                                                       , suffix = 0
                                                                                       , quiet = 1
                                                                                       )

                                 })
        self.env_manager.update_environment_vars(environment_reqs)

