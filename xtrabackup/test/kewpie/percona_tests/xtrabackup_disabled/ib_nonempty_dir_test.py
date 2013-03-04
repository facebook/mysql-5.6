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
# Test for bug https://bugs.launchpad.net/percona-xtrabackup/+bug/737569

    def setUp(self):
        master_server = servers[0] # assumption that this is 'master'
        backup_path = os.path.join(master_server.vardir, '_xtrabackup')
        # remove backup path
        if os.path.exists(backup_path):
            shutil.rmtree(backup_path)


    def test_ib_nonempty_dir(self):
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

            # take a backup
            cmd = ("%s --defaults-file=%s --user=root --port=%d"
                   " --host=127.0.0.1 --no-timestamp" 
                   " --ibbackup=%s %s" %( innobackupex
                                       , master_server.cnf_file
                                       , master_server.master_port
                                       , xtrabackup
                                       , backup_path))
            retcode, output = self.execute_cmd(cmd, output_path, exec_path, True)
            self.assertTrue(retcode==0,output)
        
            # shutdown our server
            master_server.stop()

            # prepare our backup
            cmd = ("%s --apply-log --no-timestamp --use-memory=500M "
                   "--ibbackup=%s %s" %( innobackupex
                                       , xtrabackup
                                       , backup_path))
            retcode, output = self.execute_cmd(cmd, output_path, exec_path, True)
            self.assertTrue(retcode==0,output)

            # NOTE: We do NOT remove the old datadir
            # This should trigger an error message upon
            # attempting to restore
 
            # restore from backup
            cmd = ("%s --defaults-file=%s --copy-back"
                  " --ibbackup=%s %s" %( innobackupex
                                       , master_server.cnf_file
                                       , xtrabackup
                                       , backup_path))
            retcode, output = self.execute_cmd(cmd, output_path, exec_path, True)
            logging.test_debug("Restored retcode: %d" %retcode)
            self.assertEqual(retcode, 255, output)
            # Check our output for the expected error message
            expected_msg = "Original data directory is not empty! at %s line" %(innobackupex)
            last_line = output.strip().split('\n')[-1]
            self.assertTrue(expected_msg in last_line, msg="Output: %s || expected message: %s" %(output, expected_msg))
            # restart the server to clean up
            master_server.start()

                    
