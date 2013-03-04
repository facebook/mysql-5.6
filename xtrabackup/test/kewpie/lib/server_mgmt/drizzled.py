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


""" drizzled.py:  code to allow a serverManager
    to provision and start up a drizzled server object
    for test execution

"""

# imports
import os
from lib.server_mgmt.server import Server

class drizzleServer(Server):
    """ represents a drizzle server, its possessions
        (datadir, ports, etc), and methods for controlling
        and querying it

        TODO: create a base server class that contains
              standard methods from which we can inherit
              Currently there are definitely methods / attr
              which are general

    """

    def __init__( self, name, server_manager, code_tree, default_storage_engine
                , server_options, requester, test_executor, workdir_root):
        super(drizzleServer, self).__init__( name
                                           , server_manager
                                           , code_tree
                                           , default_storage_engine
                                           , server_options
                                           , requester
                                           , test_executor
                                           , workdir_root)
        self.preferred_base_port = 9306
                
        # client files
        self.drizzledump = self.code_tree.drizzledump
        self.drizzle_client = self.code_tree.drizzle_client
        self.drizzleimport = self.code_tree.drizzleimport
        self.drizzleslap = self.code_tree.drizzleslap
        self.server_path = self.code_tree.drizzle_server
        self.drizzle_client_path = self.code_tree.drizzle_client
        self.schemawriter = self.code_tree.schemawriter
        self.trx_reader = self.code_tree.trx_reader

        # Get our ports
        self.port_block = self.system_manager.port_manager.get_port_block( self.name
                                                                         , self.preferred_base_port
                                                                         , 6 )
        self.master_port = self.port_block[0]
        self.drizzle_tcp_port = self.port_block[1]
        self.mc_port = self.port_block[2]
        self.pbms_port = self.port_block[3]
        self.rabbitmq_node_port = self.port_block[4]
        self.json_server_port = self.port_block[5]

        # Generate our working directories
        self.dirset = {'var_%s' %(self.name): {'std_data_ln':( os.path.join(self.code_tree.testdir,'std_data'))
                                               ,'log':None
                                               ,'run':None
                                               ,'tmp':None
                                               ,'master-data': {'local': { 'test':None
                                                                         , 'mysql':None
                                                                         }
                                                               }
                                               }  
                      }
        self.workdir = self.system_manager.create_dirset( workdir_root
                                                        , self.dirset)
        self.vardir = self.workdir
        self.tmpdir = os.path.join(self.vardir,'tmp')
        self.rundir = os.path.join(self.vardir,'run')
        self.logdir = os.path.join(self.vardir,'log')
        self.datadir = os.path.join(self.vardir,'master-data')

        self.error_log = os.path.join(self.logdir,'error.log')
        self.pid_file = os.path.join(self.rundir,('%s.pid' %(self.name)))
        self.socket_file = os.path.join(self.vardir, ('%s.sock' %(self.name)))
        if len(self.socket_file) > 107:
            # MySQL has a limitation of 107 characters for socket file path
            # we copy the mtr workaround of creating one in /tmp
            self.logging.verbose("Default socket file path: %s" %(self.socket_file))
            self.socket_file = "/tmp/%s_%s.%s.sock" %(self.system_manager.uuid
                                                    ,self.owner
                                                    ,self.name)
            self.logging.verbose("Changing to alternate: %s" %(self.socket_file))
        self.timer_file = os.path.join(self.logdir,('timer'))

        # Do magic to create a config file for use with the slave
        # plugin
        self.slave_config_file = os.path.join(self.logdir,'slave.cnf')
        self.create_slave_config_file()

        self.snapshot_path = os.path.join(self.tmpdir,('snapshot_%s' %(self.master_port)))
        # We want to use --secure-file-priv = $vardir by default
        # but there are times / tools when we need to shut this off
        if self.no_secure_file_priv:
            self.secure_file_string = ''
        else:
            self.secure_file_string = "--secure-file-priv='%s'" %(self.vardir)
        self.user_string = '--user=root'

        self.initialize_databases()
        self.take_db_snapshot()

        self.logging.debug_class(self)

    def report(self):
        """ We print out some general useful info """
        report_values = [ 'name'
                        , 'master_port'
                        , 'drizzle_tcp_port'
                        , 'mc_port'
                        , 'pbms_port'
                        , 'rabbitmq_node_port'
                        , 'vardir'
                        , 'status'
                        ]
        self.logging.info("%s server:" %(self.owner))
        for key in report_values:
          value = vars(self)[key] 
          self.logging.info("%s: %s" %(key.upper(), value))

    def get_start_cmd(self):
        """ Return the command string that will start up the server 
            as desired / intended
 
        """

        server_args = [ self.process_server_options()
                      , "--mysql-protocol.port=%d" %(self.master_port)
                      , "--mysql-protocol.connect-timeout=60"
                      , "--innodb.data-file-path=ibdata1:20M:autoextend"
                      , "--sort-buffer-size=256K"
                      , "--max-heap-table-size=1M"
                      , "--mysql-unix-socket-protocol.path=%s" %(self.socket_file)
                      , "--pid-file=%s" %(self.pid_file)
                      , "--drizzle-protocol.port=%d" %(self.drizzle_tcp_port)
                      , "--default-storage-engine=%s" %(self.default_storage_engine)
                      , "--datadir=%s" %(self.datadir)
                      , "--tmpdir=%s" %(self.tmpdir)
                      , self.secure_file_string
                      , self.user_string
                      ]

        if self.gdb:
            server_args.append('--gdb')
            return self.system_manager.handle_gdb_reqs(self, server_args)
        else:
            return "%s %s %s & " % ( self.cmd_prefix
                                   , self.server_path
                                   , " ".join(server_args)
                                   )


    def get_stop_cmd(self):
        """ Return the command that will shut us down """
        
        return "%s --user=root --port=%d --connect-timeout=5 --silent --password= --shutdown " %(self.drizzle_client_path, self.master_port)
           

    def get_ping_cmd(self):
        """Return the command string that will 
           ping / check if the server is alive 

        """

        return "%s --ping --port=%d --user=root" % (self.drizzle_client_path, self.master_port)

    def is_started(self):
        """ Determine if the server is up and running - 
            this may vary from server type to server type

        """

        # We experiment with waiting for a pid file to be created vs. pinging
        # This is what test-run.pl does and it helps us pass logging_stats tests
        # while not self.ping_server(server, quiet=True) and timer != timeout:

        return self.system_manager.find_path( [self.pid_file]
                                            , required=0)

    def create_slave_config_file(self):
       """ Create a config file suitable for use
           with the slave-plugin.  This allows
           us to tie other servers in easily

       """

       config_data = [ "[master1]"
                     , "master-host=127.0.0.1"
                     , "master-port=%d" %self.master_port
                     , "master-user=root"
                     , "master-pass=''"
                     , "max-reconnects=100"
                     #, "seconds-between-reconnects=20"
                     ]
       outfile = open(self.slave_config_file,'w')
       for line in config_data:
           outfile.write("%s\n" %(line))
       outfile.close()




                  




 
         




