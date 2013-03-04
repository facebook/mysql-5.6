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


""" mysqld.py:  code to allow a serverManager
    to provision and start up a mysqld server object
    for test execution

"""

# imports
import os
import sys
import time
import platform
import subprocess

from ConfigParser import RawConfigParser

from lib.server_mgmt.server import Server
from lib.util.mysql_methods import execute_query

class mysqlServer(Server):
    """ represents a mysql server, its possessions
        (datadir, ports, etc), and methods for controlling
        and querying it

        TODO: create a base server class that contains
              standard methods from which we can inherit
              Currently there are definitely methods / attr
              which are general

    """

    def __init__( self, name, server_manager, code_tree, default_storage_engine
                , server_options, requester, test_executor, workdir_root):
        super(mysqlServer, self).__init__( name
                                           , server_manager
                                           , code_tree
                                           , default_storage_engine
                                           , server_options
                                           , requester
                                           , test_executor
                                           , workdir_root)
        self.preferred_base_port = 9306
        
        # client files
        self.mysqldump = self.code_tree.mysqldump
        self.mysqladmin = self.code_tree.mysqladmin
        self.mysql_client = self.code_tree.mysql_client
        self.mysqlimport = self.code_tree.mysqlimport
        self.mysqlslap = self.code_tree.mysqlslap
        self.mysql_upgrade = self.code_tree.mysql_upgrade
        self.server_path = self.code_tree.mysql_server
        self.mysql_client_path = self.code_tree.mysql_client

        # galera-specific info
        self.wsrep_cluster_addr=''
        self.client_init_command = "SET wsrep_causal_reads=1"
      
        # important stuff
        self.langdir = self.code_tree.langdir
        self.charsetdir = self.code_tree.charsetdir
        self.bootstrap_file = self.code_tree.bootstrap_path
        self.bootstrap_cmd = None

        # Get our ports
        self.port_block = self.system_manager.port_manager.get_port_block( self.name
                                                                         , self.preferred_base_port
                                                                         , 3 )
        self.master_port = self.port_block[0]
        self.galera_listen_port = self.port_block[1]
        self.galera_recv_port = self.port_block[2]

        # Generate our working directories
        self.dirset = { 'var_%s' %(self.name): {'std_data_ln':( os.path.join(self.code_tree.testdir,'std_data'))
                                               ,'log':None
                                               ,'run':None
                                               ,'tmp':None
                                               ,'master-data': { 'test':None
                                                               , 'mysql':None
                                                               }
                                               }  
                      }
        self.workdir = self.system_manager.create_dirset( workdir_root
                                                        , self.dirset)
        self.vardir = self.workdir
        self.tmpdir = os.path.join(self.vardir,'tmp')
        self.rundir = os.path.join(self.vardir,'run')
        self.logdir = os.path.join(self.vardir,'log')
        self.std_data = os.path.join(self.vardir,'std_data_ln')
        self.datadir = os.path.join(self.vardir,'master-data')

        self.error_log = os.path.join(self.logdir,('error.log' ))
        self.bootstrap_log = os.path.join(self.logdir,('bootstrap.log'))
        self.pid_file = os.path.join(self.rundir,('my.pid'))
        self.socket_file = os.path.join(self.vardir, ('my.sock'))
        if len(self.socket_file) > 107:
            # MySQL has a limitation of 107 characters for socket file path
            # we copy the mtr workaround of creating one in /tmp
            self.logging.verbose("Default socket file path: %s" %(self.socket_file))
            self.socket_file = "/tmp/%s_%s.%s.sock" %(self.system_manager.uuid
                                                    ,self.owner
                                                    ,self.name)
            self.logging.verbose("Changing to alternate: %s" %(self.socket_file))

        self.timer_file = os.path.join(self.logdir,('timer'))
        self.general_log_file = os.path.join(self.logdir,'mysqld.log')
        self.slow_query_log_file = os.path.join(self.logdir,'mysqld-slow.log')
        self.cnf_file = os.path.join(self.vardir,'my.cnf')

        self.snapshot_path = os.path.join(self.tmpdir,('snapshot_%s' %(self.master_port)))
        # We want to use --secure-file-priv = $vardir by default
        # but there are times / tools when we need to shut this off
        if self.no_secure_file_priv:
            self.secure_file_string = ''
        else:
            self.secure_file_string = "--secure-file-priv='%s'" %(self.vardir)

        self.initialize_databases()
        self.take_db_snapshot()

        self.logging.debug_class(self)


    def report(self):
        """ We print out some general useful info """
        report_values = [ 'name'
                        , 'master_port'
                        , 'galera_listen_port'
                        , 'galera_recv_port'
                        , 'socket_file'
                        , 'vardir'
                        , 'status'
                        ]
        self.logging.info("%s server:" %(self.owner))
        for key in report_values:
          value = vars(self)[key] 
          self.logging.info("%s: %s" %(key.upper(), value))

    def initialize_databases(self):
        """ Do the voodoo required to have a working database setup.
            For MySQL, this is calling the server with the 
            --bootstrap argument.  We generate the bootstrap
            file during codeTree intialization as the file is standard for
            all MySQL servers that are spawned from a single codeTree

        """
  
        # generate the bootstrap startup command
        if not self.bootstrap_cmd:
            mysqld_args = [ "--no-defaults"
                          , "--bootstrap"
                          , "--basedir=%s" %(self.code_tree.basedir)
                          , "--datadir=%s" %(self.datadir)
                          , "--loose-skip-falcon"
                          , "--loose-skip-ndbcluster"
                          , "--tmpdir=%s" %(self.tmpdir)
                          , "--core-file"
                          , "--lc-messages-dir=%s" %(self.langdir)
                          , "--character-sets-dir=%s" %(self.charsetdir)
                          ]
            # We add server_path into the mix this way as we
            # may alter how we store / handle server args later
            mysqld_args.insert(0,self.server_path)
            self.bootstrap_cmd = " ".join(mysqld_args)
        # execute our command
        bootstrap_log = open(self.bootstrap_log,'w')
        # open our bootstrap file
        bootstrap_in = open(self.bootstrap_file,'r')
        bootstrap_subproc = subprocess.Popen( self.bootstrap_cmd
                                            , shell=True
                                            , stdin=bootstrap_in
                                            , stdout=bootstrap_log
                                            , stderr=bootstrap_log
                                            )
        bootstrap_subproc.wait()
        bootstrap_in.close()
        bootstrap_log.close()
        bootstrap_retcode = bootstrap_subproc.returncode
        if bootstrap_retcode:
            self.logging.error("Received retcode: %s executing command: %s"
                               %(bootstrap_retcode, self.bootstrap_cmd))
            self.logging.error("Check the bootstrap log: %s" %(self.bootstrap_log))
            sys.exit(1)


    def get_start_cmd(self):
        """ Return the command string that will start up the server 
            as desired / intended
 
        """

       # We make ourselves bug-compatible with MTR / xtrabackup
        # test runners
        if platform.system() != 'Windows' and os.geteuid() == 0:
            self.server_options.append("--user=root")

        # we need to do some wsrep voodoo for galera rpl to work
        self.ip_address = self.system_manager.get_ip_address() 
        self.listen_addr_string = "?gmcast.listen_addr=tcp://127.0.0.1:%d" %(self.galera_listen_port)
        # test for wsrep_provider argument
        wsrep_provider_string ='' 
        if self.system_manager.wsrep_provider_path:
            wsrep_provider_string = "--wsrep_provider=%s" %(self.system_manager.wsrep_provider_path)
        # handle wsrep_cluster_address argument
        wsrep_cluster_string ="--wsrep_cluster_address='gcomm://%s%s'" %( self.wsrep_cluster_addr
                                                                        , self.listen_addr_string) 
        for server_opt in self.server_options:
            if server_opt.startswith("--wsrep_cluster_address="):
                # we have a user-specified value, so we don't
                # try to insert something
                wsrep_cluster_string = '' 
                # check to see if they have specified a listen port
                # we want to use our own, so we have a better idea of
                # what is going on
                if '?gmcast.listen_addr' in server_opt:
                    pass
                else:
                    server_opt = server_opt.strip()+ listen_addr
        server_args = [ self.process_server_options()
                      , "%s" %(wsrep_cluster_string)
                      , "%s" %(wsrep_provider_string)
                      , "--wsrep_debug=ON"
                      , "--wsrep_provider_options='ist.recv_addr=%s:%d'" %(self.ip_address, self.galera_recv_port)
                      , "--wsrep_sst_receive_address='127.0.0.1:%d'" %( self.master_port)
                      , "--wsrep_sst_auth='root:'"
                      , "--wsrep_node_name='node%d'" %(self.galera_listen_port)
                      , "--bind-address=0.0.0.0" 
                      , "--innodb_locks_unsafe_for_binlog=1"
                      , "--open-files-limit=1024"
                      , "--query-cache-size=0"
                      , "--local-infile"
                      , "--character-set-server=latin1"
                      , "--connect-timeout=60"
                      , "--log-bin-trust-function-creators=1"
                      , "--key_buffer_size=1M"
                      , "--sort_buffer=256K"
                      , "--max_heap_table_size=1M"
                      , "--loose-innodb_data_file_path=ibdata1:10M:autoextend"
                      , "--loose-innodb_buffer_pool_size=8M"
                      , "--loose-innodb_write_io_threads=2"
                      , "--loose-innodb_read_io_threads=2"
                      , "--loose-innodb_log_buffer_size=1M"
                      , "--loose-innodb_log_file_size=5M"
                      , "--loose-innodb_additional_mem_pool_size=1M"
                      , "--loose-innodb_log_files_in_group=2"
                      , "--loose-innodb_lock_wait_timeout=9999999"
                      , "--slave-net-timeout=120"
                      , "--log-bin=%s" %(os.path.join(self.logdir,"mysqld-bin"))
                      , "--binlog-direct-non-transactional-updates"
                      , "--binlog-format=ROW"
                      , "--general_log=1"
                      , "--general_log_file=%s" %(self.general_log_file)
                      , "--slow_query_log=1"
                      , "--slow_query_log_file=%s" %(self.slow_query_log_file)
                      , "--basedir=%s" %(self.code_tree.basedir)
                      , "--datadir=%s" %(self.datadir)
                      , "--tmpdir=%s"  %(self.tmpdir)
                      , "--character-sets-dir=%s" %(self.charsetdir)
                      , "--lc-messages-dir=%s" %(self.langdir)
                      , "--port=%d" %(self.master_port)
                      , "--socket=%s" %(self.socket_file)
                      , "--pid-file=%s" %(self.pid_file)
                      , "--default-storage-engine=%s" %(self.default_storage_engine)
                      # server-id maybe needs fixing, but we want to avoid
                      # the server-id=0 and no replication thing...
                      , "--server-id=%d" %(self.get_numeric_server_id()+1)
                      , self.secure_file_string
                      ]
        self.gen_cnf_file(server_args)

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
        
        return "%s --no-defaults --user=root --port=%d --host=127.0.0.1 --protocol=tcp shutdown " %(self.mysqladmin, self.master_port)
           

    def get_ping_cmd(self):
        """Return the command string that will 
           ping / check if the server is alive 

        """

        return '%s --no-defaults --user=root --port=%d --host=127.0.0.1 --protocol=tcp ping' % (self.mysqladmin, self.master_port)

    def is_started(self):
        """ Determine if the server is up and running - 
            this may vary from server type to server type

        """

        # We experiment with waiting for a pid file to be created vs. pinging
        # This is what test-run.pl does and it helps us pass logging_stats tests
        # while not self.ping_server(server, quiet=True) and timer != timeout:

        return self.system_manager.find_path( [self.pid_file]
                                            , required=0)

    def gen_cnf_file(self, server_args):
        """ We generate a .cnf file for the server based
            on the arguments.  We currently don't use
            this for much, but xtrabackup uses it, so 
            we must produce one.  This could also be
            helpful for testing / etc

        """

        config_file = open(self.cnf_file,'w')
        config_file.write('[mysqld]') 
        for server_arg in server_args:
            # We currently have a list of string values
            # We need to remove any '--' stuff 
            server_arg = server_arg.replace('--','')+'\n'
            config_file.write(server_arg)
        config_file.close() 

    def set_master(self, master_server, get_cur_log_pos = True):
        """ We update things so that we use the provided master_server
            as our reference point for a new cluster / etc

        """

        self.wsrep_cluster_addr = '127.0.0.1:%d' %(master_server.galera_listen_port)

    def slave_ready(self):
        """ Return True / False based on wsrep_ready status variable """

        query = "SHOW STATUS LIKE 'wsrep_ready'"
        retcode, result = execute_query(query, self)
        if result == (('wsrep_ready', 'ON'),):
            return True
        return False
