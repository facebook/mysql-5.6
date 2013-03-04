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

server_requirements = [['--innodb-file-per-table'
                       ,'--innodb_strict_mode'
                       ,'--innodb_file_format=Barracuda'
                      ]]
servers = []
server_manager = None
test_executor = None
# we explicitly use the --no-timestamp option
# here.  We will be using a generic / vanilla backup dir
backup_path = None

def skip_checks(system_manager):
    if not system_manager.code_manager.test_tree.innodb_version or not system_manager.code_manager.test_tree.xtradb_version:
            return True, "Test requires XtraDB or Innodb plugin."
    return False, ''


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


    def test_ib_incremental(self):
        self.servers = servers
        master_server = servers[0]
        logging = test_executor.logging
        retcode, innodb_version = master_server.get_innodb_version()
        if not innodb_version:
            logging.warning("Test requires XtraDB or Innodb plugin, skipping test...")
            return

        else:
            innobackupex = test_executor.system_manager.innobackupex_path
            xtrabackup = test_executor.system_manager.xtrabackup_path
            master_server = servers[0] # assumption that this is 'master'
            backup_path = os.path.join(master_server.vardir, 'full_backup')
            inc_backup_path = os.path.join(master_server.vardir, 'inc_backup')
            output_path = os.path.join(master_server.vardir, 'innobackupex.out')
            exec_path = os.path.dirname(innobackupex)
            table_name = "`test`"
            page_sizes = [1,2,4,8,16]

            for page_size in page_sizes:
                self.setUp() 
                # populate our server with a test bed
                queries = ["DROP TABLE IF EXISTS %s" %(table_name)
                          ,("CREATE TABLE %s "
                            "(`a` int(11) DEFAULT NULL, "
                            "`number` int(11) DEFAULT NULL) "
                            " ENGINE=InnoDB DEFAULT CHARSET=latin1"
                            %(table_name)
                           )
                          ,("ALTER TABLE test ENGINE=InnoDB "
                            "ROW_FORMAT=compressed KEY_BLOCK_SIZE=%d" %page_size
                           )
                          ]
                retcode, result = self.execute_queries(queries, master_server)
                self.assertEqual(retcode, 0, msg = result) 
                row_count = 100
                self.load_table(table_name, row_count, master_server)
        
                # take a backup
                cmd = [ xtrabackup
                      , "--defaults-file=%s" %master_server.cnf_file
                      , "--datadir=%s" %master_server.datadir
                      , "--backup"
                      , "--target-dir=%s" %backup_path
                      ]
                cmd = " ".join(cmd)
                retcode, output = self.execute_cmd(cmd, output_path, exec_path, True)
                self.assertEqual(retcode,0,output)

                # load more data
                row_count = 500
                self.load_table(table_name, row_count, master_server)

                # Get a checksum for our table
                query = "CHECKSUM TABLE %s" %table_name
                retcode, orig_checksum = self.execute_query(query, master_server)
                self.assertEqual(retcode, 0, msg=result)
                logging.test_debug("Original checksum: %s" %orig_checksum)

                # Take an incremental backup
                cmd = [ xtrabackup
                      , "--defaults-file=%s" %master_server.cnf_file
                      , "--datadir=%s" %master_server.datadir
                      , "--backup"
                      , "--target-dir=%s" %inc_backup_path
                      , "--incremental-basedir=%s" %backup_path
                      ]
                cmd = " ".join(cmd)
                logging.test_debug(cmd)
                retcode, output = self.execute_cmd(cmd, output_path, exec_path, True)
                self.assertEqual(retcode,0,output)

                # Clear our table so we know the backup restored
                query = "DELETE FROM %s" %table_name
                retcode, result = self.execute_query(query,master_server)
                self.assertEqual(retcode, 0, result) 
        
                # shutdown our server
                master_server.stop()

                # prepare our main backup
                cmd = [ xtrabackup
                      , "--prepare"
                      , "--apply-log-only"
                      , "--datadir=%s" %master_server.datadir
                      , "--use-memory=500M"
                      , "--target-dir=%s" %backup_path
                      ]
                cmd = " ".join(cmd)
                retcode, output = self.execute_cmd(cmd, output_path, exec_path, True)
                self.assertTrue(retcode==0,output)

                # prepare our incremental backup
                cmd = [ xtrabackup
                      , "--prepare"
                      , "--apply-log-only"
                      , "--datadir=%s" %master_server.datadir
                      , "--use-memory=500M"
                      , "--target-dir=%s" %backup_path
                      , "--incremental-dir=%s" %(inc_backup_path)
                      ]
                cmd = " ".join(cmd)
                retcode, output = self.execute_cmd(cmd, output_path, exec_path, True)
                self.assertTrue(retcode==0,output)

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

                # Get a checksum for our table
                query = "CHECKSUM TABLE %s" %table_name
                retcode, restored_checksum = self.execute_query(query, master_server)
                self.assertEqual(retcode, 0, msg=restored_checksum)
                logging.test_debug("Restored checksum: %s" %restored_checksum)

                self.assertEqual(orig_checksum, restored_checksum, msg = "Orig: %s | Restored: %s" %(orig_checksum, restored_checksum))
     


