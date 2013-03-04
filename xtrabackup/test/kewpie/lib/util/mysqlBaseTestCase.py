#! /usr/bin/env python
# -*- mode: python; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
#
# Copyright (C) 2011 Patrick Crews
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

import unittest
import os
import time
import difflib
import subprocess

import MySQLdb
# silence annoying errors
from warnings import filterwarnings
filterwarnings('ignore', category = MySQLdb.Warning)

servers = None

class mysqlBaseTestCase(unittest.TestCase):

    def setUp(self):
        """ If we need to do anything pre-test, we do it here.
            Any code here is executed before any test method we
            may execute
    
        """
        self.servers = servers
        return


    def tearDown(self):
            #server_manager.reset_servers(test_executor.name)
            queries = ["DROP SCHEMA IF EXISTS test"
                      ,"CREATE SCHEMA IF NOT EXISTS test"
                      ]
            for server in self.servers:
                retcode, result = self.execute_queries(queries, server, schema='mysql')
                self.assertEqual(retcode,0,result)

    # Begin our utility code here
    # This is where we add methods that enable a test to do magic : )

    def execute_cmd(self, cmd, stdout_path, exec_path=None, get_output=False):
        stdout_file = open(stdout_path,'w')
        cmd_subproc = subprocess.Popen( cmd
                                      , shell=True
                                      , cwd=exec_path
                                      , stdout = stdout_file
                                      , stderr = subprocess.STDOUT
                                      )
        cmd_subproc.wait()
        retcode = cmd_subproc.returncode
        stdout_file.close()
        if get_output:
            data_file = open(stdout_path,'r')
            output = ''.join(data_file.readlines())
        else:
            output = None
        return retcode, output

    def get_tables(self, server, schema):
        """ Return a list of the tables in the
            schema on the server
    
        """
        results = []
        query = "SHOW TABLES IN %s" %(schema)
        retcode, table_set = self.execute_query(query, server)
        for table_data in table_set:
            table_name = table_data[0]
            results.append(table_name)
        return results

    def check_slaves_by_query( self
                             , master_server
                             , other_servers
                             , query
                             , expected_result = None
                             ):
        """ We execute the query across all servers
            and return a dict listing any diffs found,
            None if all is good.
    
            If a user provides an expected_result, we
            will skip executing against the master
            This is done as it is assumed the expected
            result has been generated / tested against
            the master

        """
        comp_results = {}
        if expected_result:
            pass # don't bother getting it
        else:
            # run against master for 'good' value
            retcode, expected_result = self.execute_query(query, master_server)
        for server in other_servers:
            retcode, slave_result = self.execute_query(query, server)
            #print "%s: expected_result= %s | slave_result= %s" % ( server.name 
            #                                                     , expected_result 
            #                                                     , slave_result_
            #                                                       )
    
            if not expected_result == slave_result:
                comp_data = "%s: expected_result= %s | slave_result= %s" % ( server.name 
                                                                           , expected_result 
                                                                           , slave_result
                                                                           )
                if comp_results.has_key(server.name):
                    comp_results[server.name].append(comp_data)
                else:
                    comp_results[server.name]=[comp_data]
        if comp_results:
            return comp_results
        return None
 

    def check_slaves_by_checksum( self
                                , master_server
                                , other_servers
                                , schemas=['test']
                                , tables=[]
                                ):
        """ We compare the specified tables (default = all)
            from the specified schemas between the 'master'
            and the other servers provided (via list)
            via CHECKSUM

            We return a dictionary listing the server
            and any tables that differed

        """
        comp_results = {}
        logging = master_server.logging
        for server in other_servers:
            for schema in schemas:
                for table in self.get_tables(master_server, schema):
                    query = "CHECKSUM TABLE %s.%s" %(schema, table)
                    retcode, master_checksum = self.execute_query(query, master_server)
                    retcode, slave_checksum = self.execute_query(query, server)
               
                    logging.test_debug ("%s: master_checksum= %s | slave_checksum= %s" % ( table
                                                                           , master_checksum
                                                                           , slave_checksum
                                                                           ))
                    logging.test_debug( '#'*80)

                    if not master_checksum == slave_checksum:
                        comp_data = "%s: master_checksum= %s | slave_checksum= %s" % ( table
                                                                                     , master_checksum
                                                                                     , slave_checksum
                                                                                     )
                        if comp_results.has_key(server.name):
                            comp_results[server.name].append(comp_data)
                        else:
                            comp_results[server.name]=[comp_data]
        if comp_results:
            return comp_results
        return None


    def take_mysqldump( self
                      , server
                      , databases=[]
                      , tables=[]
                      , dump_path = None
                      , cmd_root = None):
        """ Take a mysqldump snapshot of the given
            server, storing the output to dump_path
 
        """
        if not dump_path:
            dump_path = os.path.join(server.vardir, 'dumpfile.dat')
    
        if cmd_root:
            dump_cmd = cmd_root
        else:
            dump_cmd = "%s --no-defaults --user=root --port=%d --host=127.0.0.1 --protocol=tcp --result-file=%s" % ( server.mysqldump
                                                                                                                   , server.master_port
                                                                                                                   , dump_path
                                                                                                                   )
            if databases:
                if len(databases) > 1:
                    # We have a list of db's that are to be dumped so we handle things
                    dump_cmd = ' '.join([dump_cmd, '--databases', ' '.join(databases)])
                else:
                   dump_cmd = ' '.join([dump_cmd, databases[0], ' '.join(tables)])

        self.execute_cmd(dump_cmd, os.devnull)


    def diff_dumpfiles(self, orig_file_path, new_file_path):
        """ diff two dumpfiles useful for comparing servers """ 
        orig_file = open(orig_file_path,'r')
        restored_file = open(new_file_path,'r')
        orig_file_data = []
        rest_file_data = []
        orig_file_data= self.filter_data(orig_file.readlines(),'Dump completed')
        rest_file_data= self.filter_data(restored_file.readlines(),'Dump completed') 
        
        server_diff = difflib.unified_diff( orig_file_data
                                          , rest_file_data
                                          , fromfile=orig_file_path
                                          , tofile=new_file_path
                                          )
        diff_output = []
        for line in server_diff:
            diff_output.append(line)
        output = '\n'.join(diff_output)
        orig_file.close()
        restored_file.close()
        return (diff_output==[]), output

    def filter_data(self, input_data, filter_text ):
        return_data = []
        for line in input_data:
            if filter_text in line.strip():
                pass
            else:
                return_data.append(line)
        return return_data

    def execute_query( self
                     , query
                     , server
                     , password=None
                     , schema='test'):
        try:
            if server.client_init_command:
                if password:
                    conn = MySQLdb.connect( host = '127.0.0.1' 
                                          , port = server.master_port
                                          , user = 'root'
                                          , passwd=password 
                                          , db = schema
                                          , init_command = server.client_init_command)
                else:
                    conn = MySQLdb.connect( host = '127.0.0.1'
                                          , port = server.master_port
                                          , user = 'root'
                                          , db = schema
                                          , init_command=server.client_init_command)
            else:
                if password:
                    conn = MySQLdb.connect( host = '127.0.0.1'
                                          , port = server.master_port
                                          , user = 'root'
                                          , passwd=password
                                          , db = schema)
                else:
                    conn = MySQLdb.connect( host = '127.0.0.1'
                                          , port = server.master_port
                                          , user = 'root'
                                          , db = schema)

            cursor = conn.cursor()
            cursor.execute(query)
            result_set =  cursor.fetchall()
            cursor.close()
        except MySQLdb.Error, e:
            return 1, ("Error %d: %s" %(e.args[0], e.args[1]))
        conn.commit()
        conn.close()
        return 0, result_set

    def execute_queries( self
                       , query_list
                       , server
                       , schema= 'test'):
        """ Execute a set of queries as a single transaction """

        results = {} 
        retcode = 0
        try:
            if server.client_init_command:
                conn = MySQLdb.connect( host = '127.0.0.1' 
                                      , port = server.master_port
                                      , user = 'root'
                                      , db = schema
                                      , init_command = server.client_init_command)
            else:
                conn = MySQLdb.connect( host = '127.0.0.1'
                                      , port = server.master_port
                                      , user = 'root'
                                      , db = schema)
            cursor = conn.cursor()
            for idx, query in enumerate(query_list):
                try:
                    cursor.execute(query)
                    result_set = cursor.fetchall()
                except MySQLdb.Error, e:
                    result_set = "Error %d: %s" %(e.args[0], e.args[1])   
                    retcode = 1
                finally:
                    results[query+str(idx)] = result_set
            conn.commit()
            cursor.close()
            conn.close()
        except Exception, e:
            retcode = 1
            results = (Exception, e)
        finally:
            return retcode, results

    def execute_randgen(self, test_cmd, test_executor, server, schema='test'):
        randgen_outfile = os.path.join(test_executor.logdir,'randgen.out')
        randgen_output = open(randgen_outfile,'w')
        server_type = test_executor.master_server.type
        if server_type in ['percona','galera']:
            # it is mysql for dbd::perl purposes
            server_type = 'mysql'
        dsn = "--dsn=dbi:%s:host=127.0.0.1:port=%d:user=root:password="":database=%s" %( server_type
                                                                                       , server.master_port
                                                                                       , schema)
        randgen_cmd = " ".join([test_cmd, dsn])
        randgen_subproc = subprocess.Popen( randgen_cmd
                                          , shell=True
                                          , cwd=test_executor.system_manager.randgen_path
                                          , env=test_executor.working_environment
                                          , stdout = randgen_output
                                          , stderr = subprocess.STDOUT
                                          )
        randgen_subproc.wait()
        retcode = randgen_subproc.returncode     
        randgen_output.close()

        randgen_file = open(randgen_outfile,'r')
        output = ''.join(randgen_file.readlines())
        randgen_file.close()
        if retcode == 0:
            if not test_executor.verbose:
                output = None
        return retcode, output

    def get_randgen_process( self
                           , cmd_sequence
                           , test_executor
                           , server
                           , schema='test'
                           , randgen_outfile=None
                           , shell_flag = False):
        """ There are times when we want finer grained control over our process
            and perhaps to kill it so it doesn't waste time running to completion
            for those cases, we have this function

        """
        if not randgen_outfile:
            randgen_outfile = os.path.join(test_executor.logdir,'randgen.out')
        randgen_output = open(randgen_outfile,'w')
        server_type = test_executor.master_server.type
        if server_type in ['percona','galera']:
            # it is mysql for dbd::perl purposes
            server_type = 'mysql'
        dsn = "--dsn=dbi:%s:host=127.0.0.1:port=%d:user=root:password="":database=%s" %( server_type
                                                                                       , server.master_port
                                                                                       , schema)
        cmd_sequence.append(dsn)
        # if we use shell=True, we need to supply a string vs. a seq.
        if shell_flag:
            cmd_sequence = " ".join(cmd_sequence)
        randgen_subproc = subprocess.Popen( cmd_sequence 
                                          , cwd=test_executor.system_manager.randgen_path
                                          , env=test_executor.working_environment
                                          , shell=shell_flag
                                          , stdout = randgen_output
                                          , stderr = subprocess.STDOUT 
                                          )
        return randgen_subproc

    def find_backup_path(self, output):
        """ Determine xtrabackup directory from output """
        backup_path = None
        output = output.split('\n')
        flag_string = "Backup created in directory"
        for line in output:
            if flag_string in line:
               backup_path = line.split(flag_string)[1].strip().replace("'",'')
        return backup_path 

       
    def wait_slaves_ready(self, master_server, slave_servers, cycles = 30):
        """ Utility func to pause until the slaves are 'ready'
            The definition of 'ready' will vary upon server
            implementation

        """
    
        while slave_servers and cycles:
            for idx, slave_server in enumerate(slave_servers):
                if slave_server.slave_ready():
                    slave_servers.pop(idx)  
            cycles -= 1
            # short sleep to avoid polling slaves in busy loop
            time.sleep(0.5)
        if cycles == 0 and slave_servers:
            raise Exception("Max cycles reached when waiting for slave servers to start")
