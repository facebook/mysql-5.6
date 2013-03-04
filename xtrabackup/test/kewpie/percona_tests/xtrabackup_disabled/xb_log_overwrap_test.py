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
import time
import shutil
import signal
import subprocess

from lib.util.mysqlBaseTestCase import mysqlBaseTestCase

server_requirements = [['--innodb_log_file_size=1M --innodb_thread_concurrency=1']]
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
        # remove backup paths
        for del_path in [backup_path]:
            if os.path.exists(del_path):
                shutil.rmtree(del_path)

    def test_xb_log_overwrap(self):
        self.servers = servers
        logging = test_executor.logging
        if servers[0].type not in ['mysql','percona']:
            return
        else:
            innobackupex = test_executor.system_manager.innobackupex_path
            xtrabackup = test_executor.system_manager.xtrabackup_path
            master_server = servers[0] # assumption that this is 'master'
            backup_path = os.path.join(master_server.vardir, 'full_backup')
            output_path = os.path.join(master_server.vardir, 'innobackupex.out')
            suspended_file = os.path.join(backup_path, 'xtrabackup_suspended')
            exec_path = os.path.dirname(innobackupex)

            # populate our server with a test bed
            test_cmd = "./gentest.pl --gendata=conf/percona/percona.zz"
            retcode, output = self.execute_randgen(test_cmd, test_executor, master_server)
        
            # take a backup
            cmd = [ xtrabackup
                  , "--defaults-file=%s" %master_server.cnf_file
                  , "--datadir=%s" %master_server.datadir
                  , "--backup"
                  , "--target-dir=%s" %backup_path
                  , "--suspend-at-end"
                  ]
            output_file = open(output_path,'w')
            xtrabackup_subproc = subprocess.Popen( cmd
                                                 , cwd=exec_path
                                                 , env=test_executor.working_environment
                                                 , stdout = output_file
                                                 , stderr = subprocess.STDOUT 
                                                 )

            # Wait for the xtrabackup_suspended file to be created
            timeout = 30
            decrement = 1
            while timeout and not os.path.exists(suspended_file):
                time.sleep(decrement)
                timeout -= decrement
            # Stop the xtrabackup process
            xtrabackup_subproc.send_signal(signal.SIGSTOP)

            # Create a large amount of log data
            for i in range(20):
                query = "CREATE TABLE tmp%d ENGINE=InnoDB SELECT * FROM DD" %i
                retcode, result = self.execute_query(query, master_server)
                self.assertEqual(retcode,0,msg = result)
            
            # Resume the xtrabackup process and remove the suspended file
            xtrabackup_subproc.send_signal(signal.SIGCONT)
            try:
                os.remove(suspended_file)
            except OSError:
                pass
            xtrabackup_subproc.wait()
            output_file.close()
            output_file = open(output_path,'r')
            output = ''.join(output_file.readlines())
            output_file.close()
            expected_warning = "xtrabackup: error: it looks like InnoDB log has wrapped around before xtrabackup could process all records due to either log copying being too slow, or  log files being too small."
            self.assertEqual(xtrabackup_subproc.returncode,1,msg=output)
            # We currently disable this check as it appears wonky
            # 1)  Was testing fine
            # 2)  No such check in original test suite
            #self.assertTrue(expected_warning in output, msg= "Expected warning: %s || %s" %(expected_warning,output))

