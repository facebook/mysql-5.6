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
        if servers[0].type not in ['mysql','percona']:
            return
        else:
            innobackupex = test_executor.system_manager.innobackupex_path
            xtrabackup = test_executor.system_manager.xtrabackup_path
            master_server = servers[0] # assumption that this is 'master'
            backup_path = os.path.join(master_server.vardir, '_xtrabackup')
            output_path = os.path.join(master_server.vardir, 'innobackupex.out')
            exec_path = os.path.dirname(innobackupex)
            orig_dumpfile = os.path.join(master_server.vardir,'orig_dumpfile')
            restored_dumpfile = os.path.join(master_server.vardir, 'restored_dumpfile')

            # populate our server with a test bed
            test_cmd = "./gentest.pl --gendata=conf/percona/percona.zz"
            retcode, output = self.execute_randgen(test_cmd, test_executor, master_server)
        
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

            # take mysqldump of our current server state

            self.take_mysqldump(master_server,databases=['test'],dump_path=orig_dumpfile)
        
            # shutdown our server
            master_server.stop()

            # prepare our backup
            cmd = ("%s --apply-log --no-timestamp --use-memory=500M "
                   "--ibbackup=%s %s" %( innobackupex
                                       , xtrabackup
                                       , backup_path))
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

            # take mysqldump of current server state
            self.take_mysqldump(master_server, databases=['test'],dump_path=restored_dumpfile)

            # diff original vs. current server dump files
            retcode, output = self.diff_dumpfiles(orig_dumpfile, restored_dumpfile)
            self.assertTrue(retcode, output)
 

