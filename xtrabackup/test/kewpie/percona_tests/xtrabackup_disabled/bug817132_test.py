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
import time
import shutil

from lib.util.mysqlBaseTestCase import mysqlBaseTestCase

def skip_checks(system_manager):
    """ We do some pre-test checks to see if we need to skip a test or not """
    
    # innobackupex --copy-back without explicit --ibbackup specification
    # defaults to 'xtrabackup'. So any build configurations other than xtradb51
    # would fail in Jenkins.
    if os.path.basename(system_manager.xtrabackup_path) != "xtrabackup":
        return True, "Test only works with xtradb51. --ibbackup spec defaults to xtrabackup"
    else:
        return False, ''

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

    def test_bug817132(self):
        """ --copy-back without explicit --ibbackup specification defaults to 'xtrabackup'. """
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

            # populate our server with a test bed
            test_cmd = "./gentest.pl --gendata=conf/percona/percona.zz"
            retcode, output = self.execute_randgen(test_cmd, test_executor, master_server)
     
            # take a backup
            cmd = [ innobackupex
                  , "--defaults-file=%s" %master_server.cnf_file
                  , "--parallel=8"
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
            retcode, output = self.execute_cmd(cmd, output_path, exec_path, True)
            self.assertTrue(retcode==0,output)

            # remove old datadir
            shutil.rmtree(master_server.datadir)
            os.mkdir(master_server.datadir)

            # We add xtrabackup to PATH
            tmp_path = os.environ['PATH']
            tmp_path = "%s:%s" %(os.path.dirname(xtrabackup),tmp_path)
            os.environ['PATH'] = tmp_path
            
            # restore from backup
            cmd = [ innobackupex
                  , "--defaults-file=%s" %master_server.cnf_file
                  , "--copy-back"
                  # We don't use --ibbackup here
                  #, "--ibbackup=%s" %(xtrabackup)
                  , "--socket=%s" %master_server.socket_file
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

              
