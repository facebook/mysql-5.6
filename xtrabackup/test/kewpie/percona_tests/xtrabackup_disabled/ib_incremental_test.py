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
import shutil

from lib.util.mysqlBaseTestCase import mysqlBaseTestCase

server_requirements = [['--innodb_file_per_table']]
servers = []
server_manager = None
test_executor = None
# we explicitly use the --no-timestamp option
# here.  We will be using a generic / vanilla backup dir
backup_path = None

class basicTest(mysqlBaseTestCase):

    def setUp(self):
        master_server = servers[0] # assumption that this is 'master'
        backup_path = os.path.join(master_server.vardir, '_xtrabackup')
        # remove backup path
        if os.path.exists(backup_path):
            shutil.rmtree(backup_path)
        os.mkdir(backup_path)

    def load_table(self, table_name, row_count, server):
        queries = []
        for i in range(row_count):
            queries.append("INSERT INTO %s VALUES (%d, %d)" %(table_name,i, row_count))
        retcode, result = self.execute_queries(queries, server)
        self.assertEqual(retcode, 0, msg=result)


    def test_ib_incremental(self):
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
            exec_path = os.path.dirname(innobackupex)
            table_name = "`test`"

            # populate our server with a test bed
            queries = ["DROP TABLE IF EXISTS %s" %(table_name)
                      ,("CREATE TABLE %s "
                        "(`a` int(11) DEFAULT NULL, "
                        "`number` int(11) DEFAULT NULL) "
                        " ENGINE=InnoDB DEFAULT CHARSET=latin1"
                        %(table_name)
                       )
                      ]
            retcode, result = self.execute_queries(queries, master_server)
            self.assertEqual(retcode, 0, msg = result) 
            row_count = 100
            self.load_table(table_name, row_count, master_server)
        
            # take a backup
            cmd = [ innobackupex
                  , "--defaults-file=%s" %master_server.cnf_file
                  , "--user=root"
                  , "--socket=%s" %master_server.socket_file
                  #, "--port=%d" %master_server.master_port
                  #, "--host=127.0.0.1"
                  #, "--no-timestamp"
                  , "--ibbackup=%s" %xtrabackup
                  , backup_path
                  ]
            cmd = " ".join(cmd)
            retcode, output = self.execute_cmd(cmd, output_path, exec_path, True)
            self.assertEqual(retcode,0,output)
            main_backup_path = self.find_backup_path(output) 

            # load more data
            row_count = 500
            self.load_table(table_name, row_count, master_server)

            # Get a checksum for our table
            query = "CHECKSUM TABLE %s" %table_name
            retcode, orig_checksum = self.execute_query(query, master_server)
            self.assertEqual(retcode, 0, msg=result)
            logging.test_debug("Original checksum: %s" %orig_checksum)

            # Take an incremental backup
            inc_backup_path = os.path.join(backup_path,'backup_inc1')
            cmd = [ innobackupex
                  , "--defaults-file=%s" %master_server.cnf_file
                  , "--user=root"
                  , "--socket=%s" %master_server.socket_file
                  #, "--port=%d" %master_server.master_port
                  #, "--host=127.0.0.1" 
                  #, "--no-timestamp"
                  , "--incremental"
                  , "--incremental-basedir=%s" %main_backup_path
                  , "--ibbackup=%s" %xtrabackup
                  , backup_path
                  ]
            cmd = " ".join(cmd)
            retcode, output = self.execute_cmd(cmd, output_path, exec_path, True)
            inc_backup_path = self.find_backup_path(output) 
            self.assertTrue(retcode==0,output)
        
            # shutdown our server
            master_server.stop()

            # prepare our main backup
            cmd = [ innobackupex
                  , "--defaults-file=%s" %master_server.cnf_file
                  , "--user=root"
                  , "--socket=%s" %master_server.socket_file
                  , "--apply-log"
                  , "--redo-only"
                  , "--use-memory=500M"
                  , "--ibbackup=%s" %xtrabackup
                  , main_backup_path
                  ]
            cmd = " ".join(cmd)
            retcode, output = self.execute_cmd(cmd, output_path, exec_path, True)
            self.assertEqual(retcode,0,output)

            # prepare our incremental backup
            cmd = [ innobackupex
                  , "--defaults-file=%s" %master_server.cnf_file
                  , "--user=root"
                  , "--socket=%s" %master_server.socket_file
                  , "--apply-log"
                  , "--redo-only"
                  , "--use-memory=500M"
                  , "--ibbackup=%s" %xtrabackup
                  , "--incremental-dir=%s" %inc_backup_path
                  , main_backup_path
                  ]
            cmd = " ".join(cmd)
            retcode, output = self.execute_cmd(cmd, output_path, exec_path, True)
            self.assertTrue(retcode==0,output)

            # do final prepare on main backup
            cmd = [ innobackupex
                  , "--defaults-file=%s" %master_server.cnf_file
                  , "--user=root"
                  , "--socket=%s" %master_server.socket_file
                  , "--apply-log"
                  , "--use-memory=500M"
                  , "--ibbackup=%s" %xtrabackup
                  , main_backup_path
                  ]
            cmd = " ".join(cmd)
            retcode, output = self.execute_cmd(cmd, output_path, exec_path, True)
            self.assertEqual(retcode, 0,msg = "Command: %s failed with output: %s" %(cmd,output))


            # remove old datadir
            shutil.rmtree(master_server.datadir)
            os.mkdir(master_server.datadir)
        
            # restore from backup
            cmd = [ innobackupex
                  , "--defaults-file=%s" %master_server.cnf_file
                  , "--user=root"
                  , "--socket=%s" %master_server.socket_file
                  , "--copy-back"
                  , "--ibbackup=%s" %xtrabackup
                  , main_backup_path
                  ]
            cmd = " ".join(cmd)
            retcode, output = self.execute_cmd(cmd, output_path, exec_path, True)
            self.assertTrue(retcode==0, output)

            # restart server (and ensure it doesn't crash)
            master_server.start()
            self.assertTrue(master_server.ping(quiet=True), 'Server failed restart from restored datadir...')

            # Get a checksum for our table
            query = "CHECKSUM TABLE %s" %table_name
            retcode, restored_checksum = self.execute_query(query, master_server)
            self.assertEqual(retcode, 0, msg=result)
            logging.test_debug("Restored checksum: %s" %restored_checksum)

            self.assertEqual(orig_checksum, restored_checksum, msg = "Orig: %s | Restored: %s" %(orig_checksum, restored_checksum))
 

