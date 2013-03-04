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
        backup_path = os.path.join(master_server.vardir, '_xtrabackup')
        # remove backup path
        if os.path.exists(backup_path):
            shutil.rmtree(backup_path)

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
            test_cmd = "./gentest.pl --gendata=conf/percona/percona.zz"
            retcode, output = self.execute_randgen(test_cmd, test_executor, master_server)

            # gather some original values for comparison
            show_tables_query = "SHOW TABLES IN test"
            retcode, show_tables_result = self.execute_query(show_tables_query, master_server)
            self.assertEqual(retcode, 0, msg = show_tables_result)
        
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
            retcode, output = self.execute_cmd(cmd, output_path, exec_path, True)
            self.assertTrue(retcode==0,output)

            # shutdown our server
            master_server.stop()

            # prepare our backup
            cmd = [ innobackupex
                  , "--apply-log"
                  , "--no-timestamp"
                  , "--use-memory=500M"
                  , "--ibbackup=%s" %xtrabackup
                  , backup_path
                  ]
            cmd = " ".join(cmd)
            retcode, output = self.execute_cmd(cmd, output_path, exec_path, True)
            self.assertTrue(retcode==0,output)

            # remove old datadir
            shutil.rmtree(master_server.datadir)
            os.mkdir(master_server.datadir)
        
            # restore from backup
            cmd = ("%s --defaults-file=%s --copy-back"
                  " --ibbackup=%s %s" %( innobackupex
                                       , master_server.cnf_file
                                       , xtrabackup
                                       , backup_path))
            retcode, output = self.execute_cmd(cmd, output_path, exec_path, True)
            self.assertTrue(retcode==0, output)

            # restart server (and ensure it doesn't crash)
            master_server.start()
            self.assertTrue(master_server.status==1, 'Server failed restart from restored datadir...')

            # gather some original values for comparison
            show_tables_query = "SHOW TABLES IN test"
            retcode, restored_show_tables_result = self.execute_query(show_tables_query, master_server)
            self.assertEqual(retcode, 0, msg = restored_show_tables_result)
            self.assertEqual(show_tables_result, restored_show_tables_result, msg = ("%s || %s" %(show_tables_result, restored_show_tables_result)))
            query = "SELECT COUNT(*) FROM DD"
            retcode, result = self.execute_query(query, master_server)
            self.assertEqual(retcode,0,msg = result)
            expected_result = ((100L,),)
            self.assertEqual(result, expected_result, msg = "%s || %s" %(expected_result, result))

            
 

