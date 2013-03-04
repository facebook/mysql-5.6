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

import os
import sys
import shutil
import subprocess

from lib.util.mysqlBaseTestCase import mysqlBaseTestCase

server_requirements = [[]]
servers = []
server_manager = None
test_executor = None
# we explicitly use the --no-timestamp option
# here.  We will be using a generic / vanilla backup dir
backup_path = None

class basicTest(mysqlBaseTestCase):

    def execute_cmd( self
                   , cmd
                   , stdout_path
                   , stderr_path
                   , exec_path=None
                   , get_output=True):
        stdout_file = open(stdout_path,'w')
        stderr_file = open(stderr_path,'w')
        cmd_subproc = subprocess.Popen( cmd
                                      , shell=True
                                      , cwd=exec_path
                                      , stdout = stdout_file
                                      , stderr = stderr_file 
                                      )
        cmd_subproc.wait()
        retcode = cmd_subproc.returncode
        stdout_file.close()
        stderr_file.close()
        if get_output:
            data_file = open(stdout_path,'r')
            output = ''.join(data_file.readlines())
        else:
            output = None
        return retcode, output

    def execute_cmd2(self, cmd, stdout_path, exec_path=None, get_output=False):
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

        
    def setUp(self):
        master_server = servers[0] # assumption that this is 'master'
        backup_path = os.path.join(master_server.vardir, '_xtrabackup')
        # remove backup path
        if os.path.exists(backup_path):
            shutil.rmtree(backup_path)

    def test_bug514068(self):
        """ Bug #514068: Output to STDOUT and STDERR is not conventional
            Bug #741021: xtrabackup --prepare prints a few lines to stdout
        """

        self.servers = servers
        logging = test_executor.logging
        if servers[0].type not in ['mysql','percona']:
            return
        else:
            innobackupex = test_executor.system_manager.innobackupex_path
            xtrabackup = test_executor.system_manager.xtrabackup_path
            master_server = servers[0] # assumption that this is 'master'
            backup_path = os.path.join(master_server.vardir, '_xtrabackup')
            output_path = os.path.join(master_server.vardir, 'innobackupex.out')
            stderr_path = os.path.join(master_server.vardir,'innobackupex.err')
            exec_path = os.path.dirname(innobackupex)

            # populate our server with a test bed
            test_cmd = "./gentest.pl --gendata=conf/percona/percona.zz"
            retcode, output = self.execute_randgen(test_cmd, test_executor, master_server)

            """
            # populate our server with a test bed
            #test_cmd = "./gentest.pl --gendata=conf/percona/percona.zz"
            #retcode, output = self.execute_randgen(test_cmd, test_executor, master_server)
            xtrabackup_basedir = os.path.dirname(innobackupex)
            datapath = os.path.join(xtrabackup_basedir,'test/inc/sakila-db')
            file_names = ['sakila-schema.sql','sakila-data.sql']
            for file_name in file_names:
                file_name = os.path.join(datapath,file_name)
                cmd = "%s -uroot --protocol=tcp --port=%d < %s" %(master_server.mysql_client, master_server.master_port, file_name)
                retcode, output = self.execute_cmd2(cmd, output_path, exec_path, True)
                self.assertEqual(retcode,0,output)
            """

     
            # take a backup
            cmd = [ innobackupex
                  , "--defaults-file=%s" %master_server.cnf_file
                  , "--user=root"
                  , "--port=%d" %master_server.master_port
                  , "--host=127.0.0.1"
                  , "--no-timestamp"
                  , "--ibbackup=%s" %xtrabackup
                  , backup_path
                  ]
            cmd = " ".join(cmd)
            retcode, output = self.execute_cmd(cmd, output_path, stderr_path, exec_path, True)
            self.assertTrue(retcode==0,output)

            # stop the server
            master_server.stop()

            # do prepare on backup
            cmd = [ innobackupex
                  , "--apply-log"
                  , "--no-timestamp"
                  , "--use-memory=500M"
                  , "--ibbackup=%s" %xtrabackup
                  , backup_path
                  ]
            cmd = " ".join(cmd)
            retcode, output = self.execute_cmd(cmd, output_path, stderr_path, exec_path, True)
            self.assertEqual(retcode, 0,output)
            # stdout is returned with output
            # we expect 0 lines
            self.assertEqual(len(output),0,output)

            # remove old datadir
            shutil.rmtree(master_server.datadir)
            os.mkdir(master_server.datadir)
        
            # restore from backup
            cmd = [ innobackupex
                  , "--defaults-file=%s" %master_server.cnf_file
                  , "--copy-back"
                  , "--ibbackup=%s" %(xtrabackup)
                  , backup_path
                  ]
            cmd = " ".join(cmd)
            retcode, output = self.execute_cmd(cmd, output_path, stderr_path, exec_path, True)
            self.assertEqual(retcode, 0,output)
            # stdout is returned with output
            # we expect 0 lines
            self.assertEqual(len(output),0,output)

            # restart server (and ensure it doesn't crash)
            master_server.start()
            self.assertEqual(master_server.status,1, 'Server failed restart from restored datadir...')
            
            # Check the server is ok
            query = "SELECT COUNT(*) FROM DD"
            #query = "SELECT COUNT(*) FROM sakila.actor"
            expected_output = ((100L,),) 
            #expected_output = ((200L,),)
            retcode, output = self.execute_query(query, master_server)
            self.assertEqual(output, expected_output, msg = "%s || %s" %(output, expected_output))

              
