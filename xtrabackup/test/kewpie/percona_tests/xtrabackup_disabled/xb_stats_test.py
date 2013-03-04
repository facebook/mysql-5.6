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

server_requirements = [[]]
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

    def test_xb_stats(self):
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

            # take a backup
            cmd = [ xtrabackup 
                  , "--defaults-file=%s" %master_server.cnf_file
                  , "--backup"
                  , "--datadir=%s" %(master_server.datadir)
                  , "--target-dir=%s" %backup_path
                  ]
            cmd = " ".join(cmd)
            retcode, output = self.execute_cmd(cmd, output_path, exec_path, True)
            self.assertEqual(retcode,0,output)
      
            # first prepare
            cmd = [ xtrabackup
                  , "--defaults-file=%s" %master_server.cnf_file
                  , "--prepare"
                  , "--datadir=%s" %(master_server.datadir)
                  , "--target-dir=%s" %backup_path
                  ]
            cmd = " ".join(cmd)
            retcode, output = self.execute_cmd(cmd, output_path, exec_path, True )
            self.assertEqual(retcode,0,output)

            # Attempt to use --stats, which should fail
            # as we haven't finished our prepare statement yet
            cmd = [ xtrabackup
                  , "--defaults-file=%s" %master_server.cnf_file
                  , "--stats"
                  , "--datadir=%s" %(backup_path)
                  ]
            cmd = " ".join(cmd)
            retcode, output = self.execute_cmd(cmd, output_path, exec_path, True)
            self.assertEqual(retcode,1,output)
            expected_output1 = "xtrabackup: Error: Cannot find log file ib_logfile0."
            expected_output2 = "xtrabackup: Error: to use the statistics feature, you need a clean copy of the database including correctly sized log files, so you need to execute with --prepare twice to use this functionality on a backup."
            output_split = output.strip().split('\n')[-2:] # last 2 lines
            line1 = output_split[0].strip()
            line2 = output_split[1].strip()
            self.assertEqual(line1, expected_output1, msg= "Expected: %s || actual: %s || full: %s" %(expected_output1, line1, output))
            self.assertEqual(line2, expected_output2, msg= "Expected: %s || actual: %s || full: %s" %(expected_output2, line2, output))

            # second prepare
            cmd = [ xtrabackup
                  , "--defaults-file=%s" %master_server.cnf_file
                  , "--prepare"
                  , "--datadir=%s" %(master_server.datadir)
                  , "--target-dir=%s" %backup_path
                  ]
            cmd = " ".join(cmd)
            retcode, output = self.execute_cmd(cmd, output_path, exec_path, True)
            self.assertEqual(retcode,0,output)

            # Attempt to use --stats, which should work this time 
            cmd = [ xtrabackup
                  , "--defaults-file=%s" %master_server.cnf_file
                  , "--stats"
                  , "--datadir=%s" %(backup_path)
                  ]
            cmd = " ".join(cmd)
            retcode, output = self.execute_cmd(cmd, output_path, exec_path, True) 
            self.assertEqual(retcode,0,output)

