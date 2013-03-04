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

server_requirements = [[]]
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
        backup_path = os.path.join(master_server.vardir, '_xtrabackup')
        # remove backup paths
        for del_path in [backup_path]:
            if os.path.exists(del_path):
                shutil.rmtree(del_path)

    def test_bug759225(self):
            self.servers = servers
            master_server = servers[0]
            logging = test_executor.logging
            innobackupex = test_executor.system_manager.innobackupex_path
            xtrabackup = test_executor.system_manager.xtrabackup_path
            master_server = servers[0] # assumption that this is 'master'
            backup_path = os.path.join(master_server.vardir, '_xtrabackup')
            tar_file_path = os.path.join(backup_path,'out.tar')
            output_path = os.path.join(master_server.vardir, 'innobackupex.out')
            exec_path = os.path.dirname(innobackupex)

            # populate our server with a test bed
            test_cmd = "./gentest.pl --gendata=conf/percona/percona.zz"
            retcode, output = self.execute_randgen(test_cmd, test_executor, master_server)

            # Add desired option to config file
            config_file = open(master_server.cnf_file,'a')
            config_file.write("innodb_flush_method=ALL_O_DIRECT\n")
            config_file.close()
     
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
            expected_output = "xtrabackup: using ALL_O_DIRECT"
            self.assertTrue(expected_output in output, msg="%s || %s" %(expected_output, output))

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
            query = "SELECT COUNT(*) FROM test.DD"
            expected_output = ((100L,),) 
            retcode, output = self.execute_query(query, master_server)
            self.assertEqual(output, expected_output, msg = "%s || %s" %(output, expected_output))

              
