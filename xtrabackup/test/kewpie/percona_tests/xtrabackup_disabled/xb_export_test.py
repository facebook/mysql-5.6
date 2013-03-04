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

server_requirements = [['--innodb-file-per-table --innodb_file_format=Barracuda']]
servers = []
server_manager = None
test_executor = None
# we explicitly use the --no-timestamp option
# here.  We will be using a generic / vanilla backup dir
backup_path = None

def skip_checks(system_manager):
    if not system_manager.code_manager.test_tree.xtradb_version:
            return True, "Test requires XtraDB."
    return False, ''

class basicTest(mysqlBaseTestCase):

    def setUp(self):
        master_server = servers[0] # assumption that this is 'master'
        backup_path = os.path.join(master_server.vardir, 'backup')
        # remove backup paths
        for del_path in [backup_path]:
            if os.path.exists(del_path):
                shutil.rmtree(del_path)

    def create_test_table(self, table_name, server):
        queries = ["DROP TABLE IF EXISTS %s" %(table_name)
                  ,("CREATE TABLE %s "
                    "(`a` int(11) DEFAULT NULL, "
                    "`number` int(11) DEFAULT NULL) "
                    " ENGINE=InnoDB DEFAULT CHARSET=latin1"
                    %(table_name)
                   )
                  ]
        retcode, result = self.execute_queries(queries, server)
        self.assertEqual(retcode, 0, msg = result) 

    def load_table(self, table_name, row_count, server):
        queries = []
        for i in range(row_count):
            queries.append("INSERT INTO %s VALUES (%d, %d)" %(table_name,i, row_count))
        retcode, result = self.execute_queries(queries, server)
        self.assertEqual(retcode, 0, msg=result)


    def test_xb_export(self):
        self.servers = servers
        master_server = servers[0]
        logging = test_executor.logging
        xtradb_version = master_server.get_xtradb_version()
        if not xtradb_version:
            logging.warning("Test requires XtraDB, skipping test...")
            return
        else:
            innobackupex = test_executor.system_manager.innobackupex_path
            xtrabackup = test_executor.system_manager.xtrabackup_path
            master_server = servers[0] # assumption that this is 'master'
            backup_path = os.path.join(master_server.vardir, 'backup')
            output_path = os.path.join(master_server.vardir, 'innobackupex.out')
            exec_path = os.path.dirname(innobackupex)
            table_name = "`test`"
            schema_name = "test"


            # This is a bit hacky.  We have a version-dependent server
            # option and no clean mechanism for doing this at test-collection
            # time
            import_option = "--innodb_expand_import=1"
            if master_server.version.startswith("5.5"):
                import_option = "--innodb_import_table_from_xtrabackup=1"
            master_server.server_options.append(import_option)
            master_server.stop()
            master_server.start()

            # populate our server with a test bed
            self.create_test_table(table_name, master_server)
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
            self.assertTrue(retcode==0,output)

            # load more data
            row_count = 100
            self.load_table(table_name, row_count, master_server)

            # Get a checksum for our table
            query = "CHECKSUM TABLE %s" %table_name
            retcode, checksum1 = self.execute_query(query, master_server)
            self.assertEqual(retcode, 0, msg=checksum1)
            logging.test_debug("Checksum1: %s" %checksum1)

            # Reset the server to treat it as the 'importing' server
            # We clean the datadir + create a table w/ similar
            # structure
            # TODO: see how things fail if we skip / mess up this step
            master_server.stop()
            master_server.restore_snapshot()
            master_server.start()
        
            # recreate the table:
            self.create_test_table(table_name, master_server)
            logging.test_debug("Server reinitialized")

            # discard the tablespace
            query = "ALTER TABLE %s DISCARD TABLESPACE" %(table_name)
            retcode, result = self.execute_query(query, master_server)
            self.assertEqual(retcode,0,msg=result)

            # prepare our main backup
            # Test the with innodb_file_per_table=0 --export bails out with an error
            # Bug #758888
            cmd = [ xtrabackup
                  , "--prepare"
                  , "--datadir=%s" %master_server.datadir
                  , "--use-memory=500M"
                  , "--target-dir=%s" %backup_path
                  , "--export"
                  , "--innodb-file-per-table=0"
                  ]
            cmd = " ".join(cmd)
            retcode, output = self.execute_cmd(cmd, output_path, exec_path, True)
            self.assertEqual(retcode,1,output)

            # prepare our main backup
            cmd = [ xtrabackup
                  , "--prepare"
                  , "--datadir=%s" %master_server.datadir
                  , "--use-memory=500M"
                  , "--target-dir=%s" %backup_path
                  , "--export"
                  , "--innodb-file-per-table=1"
                  ]
            cmd = " ".join(cmd)
            retcode, output = self.execute_cmd(cmd, output_path, exec_path, True)
            self.assertEqual(retcode,0,output)

            # copy our data files back
            db_path = os.path.join(backup_path,schema_name)
            for bkp_file in os.listdir(db_path):
                if bkp_file.startswith('test'):
                    shutil.copy(os.path.join(db_path,bkp_file)
                               ,os.path.join(master_server.datadir,schema_name)
                               )

            # import the tablespace
            query = "ALTER TABLE %s IMPORT TABLESPACE" %(table_name)
            retcode, result = self.execute_query(query, master_server)
            self.assertEqual(retcode,0,msg=result)
            logging.test_debug("Tablespace imported...")

            # Tablespace import is asynchronous, so shutdown the server to have
            # consistent backup results. Otherwise we risk ending up with no test.ibd
            # in the backup in case importing has not finished before taking backup
            master_server.stop()
            master_server.start()
            self.assertTrue(master_server.status==1, 'Server failed restart from restored datadir...')

            query="SELECT COUNT(*) FROM %s" %(table_name)
            retcode, result = self.execute_query(query, master_server)
            self.assertEqual(retcode,0,result)
            expected_result = ((100L,),)
            self.assertEqual(expected_result, result, msg = "%s || %s" %(expected_result, result))

            query = "SHOW CREATE TABLE %s" %(table_name)
            retcode, result = self.execute_query(query, master_server)
            self.assertEqual(retcode,0,result)
            expected_result = (('test', 'CREATE TABLE `test` (\n  `a` int(11) DEFAULT NULL,\n  `number` int(11) DEFAULT NULL\n) ENGINE=InnoDB DEFAULT CHARSET=latin1'),)
            self.assertEqual(expected_result, result, msg = "%s || %s" %(expected_result, result))

            # Get a checksum for our table
            query = "CHECKSUM TABLE %s" %table_name
            retcode, checksum2 = self.execute_query(query, master_server)
            self.assertEqual(retcode, 0, msg=checksum2)
            logging.test_debug("Checksum2: %s" %checksum2)
            if checksum1 != checksum2:
                logging.warning("Initial and exported/restored checksums do not match!")
                logging.warning("Checksum1:  %s" %checksum1)
                logging.warning("Checksum2:  %s" %checksum2)
            #self.assertEqual(checksum1,checksum2,msg="%s || %s" %(checksum1,checksum2))

            # create a dir to hold the new backup
            backup_path = os.path.join(backup_path,'new_backup')
            # take a backup of the imported table
            cmd = [ xtrabackup
                  , "--defaults-file=%s" %master_server.cnf_file
                  , "--datadir=%s" %master_server.datadir
                  , "--backup"
                  , "--target-dir=%s" %backup_path
                  ]
            cmd = " ".join(cmd)
            retcode, output = self.execute_cmd(cmd, output_path, exec_path, True)
            self.assertTrue(retcode==0,output)

            # Clear our table so we know the backup restored
            for del_table in [table_name]:
                query = "DELETE FROM %s" %del_table
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
            retcode, checksum3 = self.execute_query(query, master_server)
            self.assertEqual(retcode, 0, msg=checksum3)
            logging.test_debug("Checksum3: %s" %checksum3)
            self.assertEqual(checksum2,checksum3,msg="%s || %s" %(checksum2,checksum3))

 

