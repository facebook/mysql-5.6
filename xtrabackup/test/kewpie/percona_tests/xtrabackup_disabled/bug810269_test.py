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
import tarfile

from lib.util.mysqlBaseTestCase import mysqlBaseTestCase

server_requirements = [["--innodb_strict_mode --innodb_file_per_table --innodb_file_format=Barracuda"]]
servers = []
server_manager = None
test_executor = None
# we explicitly use the --no-timestamp option
# here.  We will be using a generic / vanilla backup dir
backup_path = None

def skip_checks(system_manager):
    if not system_manager.code_manager.test_tree.innodb_version:
            return True, "Test requires XtraDB or Innodb plugin."
    return False, ''


class basicTest(mysqlBaseTestCase):

    def setUp(self):
        master_server = servers[0] # assumption that this is 'master'
        backup_path = os.path.join(master_server.vardir, '_xtrabackup')
        # remove backup paths
        for del_path in [backup_path]:
            if os.path.exists(del_path):
                shutil.rmtree(del_path)

    def load_table(self, table_name, row_count, server):
        queries = []
        for i in range(row_count):
            queries.append("INSERT INTO %s VALUES (%d, %d)" %(table_name,i, row_count))
        retcode, result = self.execute_queries(queries, server)
        self.assertEqual(retcode, 0, msg=result)

    def test_bug810269(self):
            """ Bug #665210: tar4ibd does not support innodb row_format=compressed
                Bug #810269: tar4ibd does not check for doublewrite buffer pages
            """

            self.servers = servers       
            master_server = servers[0]
            logging = test_executor.logging
            innobackupex = test_executor.system_manager.innobackupex_path
            xtrabackup = test_executor.system_manager.xtrabackup_path
            backup_path = os.path.join(master_server.vardir, '_xtrabackup')
            tar_file_path = os.path.join(backup_path,'out.tar')
            output_path = os.path.join(master_server.vardir, 'innobackupex.out')
            exec_path = os.path.dirname(innobackupex)
            table_name = "t1"

            # populate our server with a test bed
            queries = ["DROP TABLE IF EXISTS %s" %(table_name)
                      ,("CREATE TABLE %s "
                        "(`a` int(11) DEFAULT NULL, "
                        "`number` int(11) DEFAULT NULL) "
                        " ENGINE=InnoDB DEFAULT CHARSET=latin1"
                        %(table_name)
                       )
                       # compress tables 
                      ,("ALTER TABLE %s ENGINE=InnoDB "
                        "ROW_FORMAT=compressed KEY_BLOCK_SIZE=4"
                        %(table_name)
                       )
                      ]
            retcode, result = self.execute_queries(queries, master_server)
            self.assertEqual(retcode, 0, msg = result) 
            row_count = 10000
            self.load_table(table_name, row_count, master_server)

            # get a checksum that we'll compare against post-restore
            query = "CHECKSUM TABLE %s" %table_name
            retcode, orig_checksum = self.execute_query(query, master_server)
            self.assertEqual(retcode, 0, orig_checksum)
     
            # take a backup
            try:
                os.mkdir(backup_path)
            except OSError: 
                pass
            cmd = [ innobackupex
                  , "--defaults-file=%s" %master_server.cnf_file
                  , "--stream=tar"
                  , "--user=root"
                  , "--port=%d" %master_server.master_port
                  , "--host=127.0.0.1"
                  , "--no-timestamp"
                  , "--ibbackup=%s" %xtrabackup
                  , "%s > %s" %(backup_path,tar_file_path)
                  ]
            cmd = " ".join(cmd)
            retcode, output = self.execute_cmd(cmd, output_path, exec_path, True)
            self.assertTrue(retcode==0,output)

            # stop the server
            master_server.stop()
 
            # extract our backup tarball
            cmd = "tar -ivxf %s" %tar_file_path
            retcode, output = self.execute_cmd(cmd, output_path, backup_path, True)
            self.assertEqual(retcode,0,output) 
            # Check for Bug 723318 - seems quicker than separate test case
            self.assertTrue('xtrabackup_binary' in os.listdir(backup_path)
                           , msg = "Bug723318:  xtrabackup_binary not included in tar archive when streaming")

            # do prepare on backup
            cmd = [ innobackupex
                  , "--apply-log"
                  , "--no-timestamp"
                  , "--use-memory=500M"
                  , "--ibbackup=%s" %xtrabackup
                  , backup_path
                  ]
            cmd = " ".join(cmd)
            retcode, output = self.execute_cmd(cmd, output_path, exec_path, True)
            self.assertEqual(retcode,0,output)

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
            retcode, output = self.execute_cmd(cmd, output_path, exec_path, True)
            self.assertEqual(retcode,0, output)

            # restart server (and ensure it doesn't crash)
            master_server.start()
            self.assertEqual(master_server.status,1, 'Server failed restart from restored datadir...')
            
            # Check the server is ok
            # get a checksum that we'll compare against pre-restore
            query = "CHECKSUM TABLE %s" %table_name
            retcode, restored_checksum = self.execute_query(query, master_server)
            self.assertEqual(retcode, 0, restored_checksum)
            self.assertEqual(orig_checksum, restored_checksum, "%s || %s" %(orig_checksum, restored_checksum))

              
