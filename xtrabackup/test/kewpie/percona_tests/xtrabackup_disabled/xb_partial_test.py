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

server_requirements = [['--innodb-file-per-table']]
servers = []
server_manager = None
test_executor = None
# we explicitly use the --no-timestamp option
# here.  We will be using a generic / vanilla backup dir
backup_path = None

class basicTest(mysqlBaseTestCase):

    def setUp(self):
        master_server = servers[0] # assumption that this is 'master'
        backup_path = os.path.join(master_server.vardir, 'full_backup')
        inc_backup_path = os.path.join(master_server.vardir, 'inc_backup')
        # remove backup paths
        for del_path in [backup_path, inc_backup_path]:
            if os.path.exists(del_path):
                shutil.rmtree(del_path)

    def load_table(self, table_name, row_count, server):
        queries = []
        for i in range(row_count):
            queries.append("INSERT INTO %s VALUES (%d, %d)" %(table_name,i, row_count))
        retcode, result = self.execute_queries(queries, server)
        self.assertEqual(retcode, 0, msg=result)

    def test_xb_partial(self):
            self.servers = servers
            logging = test_executor.logging
            innobackupex = test_executor.system_manager.innobackupex_path
            xtrabackup = test_executor.system_manager.xtrabackup_path
            master_server = servers[0] # assumption that this is 'master'
            backup_path = os.path.join(master_server.vardir, 'full_backup')
            inc_backup_path = os.path.join(master_server.vardir, 'inc_backup')
            output_path = os.path.join(master_server.vardir, 'innobackupex.out')
            exec_path = os.path.dirname(innobackupex)
            table_name = "`test`"

            # populate our server with a test bed
            queries = ["DROP TABLE IF EXISTS %s" %(table_name)
                      ,("CREATE TABLE %s "
                        "(`a` int(11) DEFAULT NULL, "
                        "`number` int(11) DEFAULT NULL) "
                        " ENGINE=InnoDB DEFAULT CHARSET=latin1 "
                        %(table_name)
                       )
                      ]
            retcode, result = self.execute_queries(queries, master_server)
            self.assertEqual(retcode, 0, msg = result) 
            row_count = 100
            self.load_table(table_name, row_count, master_server)

            # Additional tables via randgen
            test_cmd = "./gentest.pl --gendata=conf/percona/percona.zz"
            retcode, output = self.execute_randgen(test_cmd, test_executor, master_server)
            #self.assertEqual(retcode, 0, msg=output)
        
            # take a backup
            cmd = [ xtrabackup
                  , "--defaults-file=%s" %master_server.cnf_file
                  , "--datadir=%s" %master_server.datadir
                  , "--backup"
                  , '--tables="^test[.]test|DD"'
                  , "--target-dir=%s" %backup_path
                  ]
            cmd = " ".join(cmd)
            retcode, output = self.execute_cmd(cmd, output_path, exec_path, True)
            self.assertTrue(retcode==0,output)

            # Get a checksum for our `test` table
            query = "CHECKSUM TABLE %s" %table_name
            retcode, orig_checksum1 = self.execute_query(query, master_server)
            self.assertEqual(retcode, 0, msg=result)
            logging.test_debug("Original checksum1: %s" %orig_checksum1)

            # Get a checksum for our `DD` table
            query = "CHECKSUM TABLE DD"
            retcode, orig_checksum2 = self.execute_query(query, master_server)
            self.assertEqual(retcode, 0, msg=result)
            logging.test_debug("Original checksum2: %s" %orig_checksum2)

            # Clear our table so we know the backup restored
            for del_table in [table_name,'DD']:
                query = "DELETE FROM %s" %del_table
                retcode, result = self.execute_query(query,master_server)
                self.assertEqual(retcode, 0, result) 

            # Remove old tables
            for table in ['A','AA','B','BB','C','CC','D']:
                query = "DROP TABLE %s" %table
                retcode, result = self.execute_query(query,master_server)
                self.assertEqual(retcode,0,result)
       
        
            # shutdown our server
            master_server.stop()

            # do final prepare on main backup
            cmd = [ xtrabackup
                  , "--prepare"
                  , "--datadir=%s" %master_server.datadir
                  , "--use-memory=500M"
                  , "--target-dir=%s" %backup_path
                  ]
            cmd = " ".join(cmd)
            retcode, output = self.execute_cmd(cmd, output_path, exec_path, True)
            self.assertTrue(retcode==0,output)
        
            # copy our data files back
            for root, dirs, files in os.walk(backup_path):
                if files:
                    file_info = root.split(backup_path)[1]
                    for file_name in files:
                        # We do a quick check to make sure
                        # no names start with '/' as os.path
                        # throws a hissy when it sees such things
                        if file_info.startswith('/'):
                            file_info = file_info[1:]
                        if file_name.startswith('/'):
                            file_name = file_name[1:]
                        to_path = os.path.join(master_server.datadir
                                              , file_info
                                              , file_name)
                        new_dir = os.path.dirname(to_path)
                        try:
                           if not os.path.exists(new_dir):
                               os.makedirs(new_dir)
                        except OSError, e:
                            logging.error("Could not create directory: %s | %s" %(new_dir, e))
                        try:
                            shutil.copy(os.path.join(root,file_name),to_path)
                        except IOError, e:
                            logging.error( "ERROR:  Could not copy file: %s | %s" %(file_name, e))

            # restart server (and ensure it doesn't crash)
            master_server.start()
            self.assertTrue(master_server.status==1, 'Server failed restart from restored datadir...')

            # Get a checksum for our test table
            query = "CHECKSUM TABLE %s" %table_name
            retcode, restored_checksum1 = self.execute_query(query, master_server)
            self.assertEqual(retcode, 0, msg=result)
            logging.test_debug("Restored checksum1: %s" %restored_checksum1)
            self.assertEqual(orig_checksum1, restored_checksum1, msg = "Orig: %s | Restored: %s" %(orig_checksum1, restored_checksum1))

            # Get a checksum for our DD table
            query = "CHECKSUM TABLE DD"
            retcode, restored_checksum2 = self.execute_query(query, master_server)
            self.assertEqual(retcode, 0, msg=result)
            logging.test_debug("Restored checksum1: %s" %restored_checksum2)
            self.assertEqual(orig_checksum2, restored_checksum2, msg = "Orig: %s | Restored: %s" %(orig_checksum2, restored_checksum2))

