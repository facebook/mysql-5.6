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
import unittest

from lib.util.mysqlBaseTestCase import mysqlBaseTestCase

def skip_checks(system_manager):
    if system_manager.code_manager.test_type != 'galera':
        return True, "Requires galera / wsrep server"
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


    def test_basic1(self):
            self.servers = servers
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
            cmd = ("%s --defaults-file=%s --galera-info --user=root --port=%d"
                   " --host=127.0.0.1 --no-timestamp" 
                   " --ibbackup=%s %s" %( innobackupex
                                       , master_server.cnf_file
                                       , master_server.master_port
                                       , xtrabackup
                                       , backup_path))
            retcode, output = self.execute_cmd(cmd, output_path, exec_path, True)
            self.assertTrue(retcode==0,output)

            # prepare our backup
            cmd = ("%s --apply-log --galera-info --no-timestamp --use-memory=500M "
                   "--ibbackup=%s %s" %( innobackupex
                                       , xtrabackup
                                       , backup_path))
            retcode, output = self.execute_cmd(cmd, output_path, exec_path, True)
            self.assertEqual(retcode, 0, msg= output)

            # Get a test value:
            query = "SHOW STATUS LIKE 'wsrep_local_state_uuid'"
            retcode, result = self.execute_query(query, master_server)
            wsrep_local_state_uuid =  result[0][1]
  
            # Get our other galera-info value
            query = "SHOW STATUS LIKE 'wsrep_last_committed'"
            retcode, result = self.execute_query(query, master_server)
            wsrep_last_committed = result[0][1] 

            # check our log
            with open(os.path.join(backup_path,'xtrabackup_galera_info'),'r') as galera_info_file:
                galera_info = galera_info_file.readline().strip()
                logged_wsrep_local_state_uuid, logged_wsrep_last_committed = galera_info.split(':')
  
            self.assertEqual( wsrep_local_state_uuid
                            , logged_wsrep_local_state_uuid
                            , msg = (wsrep_local_state_uuid, logged_wsrep_local_state_uuid)
                            )

            self.assertEqual( wsrep_last_committed
                            , logged_wsrep_last_committed
                            , msg = (wsrep_last_committed, logged_wsrep_last_committed)
                            )

