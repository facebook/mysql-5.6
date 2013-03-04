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
from lib.util.mysql_methods import execute_cmd

def skip_checks(system_manager):
    # test to ensure we have a debug build
    cmd = "%s --help" %system_manager.xtrabackup_path 
    output_path = os.path.join(system_manager.workdir, 'innobackupex.out')
    exec_path = system_manager.workdir

    retcode, output = execute_cmd(cmd, output_path, exec_path, True)
    for line in output:
        if 'debug-sync' in line and 'TRUE' in line:
            return False, ''
        else: 
            return True, "Requires --debug-sync support."

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
        backup_path = os.path.join(master_server.vardir, 'full_backup')
        # remove backup paths
        for del_path in [backup_path]:
            if os.path.exists(del_path):
                shutil.rmtree(del_path)

    def test_bug722638(self):
            self.servers = servers
            logging = test_executor.logging
            innobackupex = test_executor.system_manager.innobackupex_path
            xtrabackup = test_executor.system_manager.xtrabackup_path
            master_server = servers[0] # assumption that this is 'master'
            backup_path = os.path.join(master_server.vardir, 'full_backup')
            output_path = os.path.join(master_server.vardir, 'innobackupex.out')
            suspended_file = os.path.join(backup_path, 'xtrabackup_debug_sync')
            exec_path = os.path.dirname(innobackupex)
        
            # populate our server with a test bed
            queries = [ "CREATE TABLE t1(a INT) ENGINE=InnoDB"
                      , "INSERT INTO t1 VALUES (1), (2), (3)"
                      , "CREATE TABLE t2(a INT) ENGINE=InnoDB"
                      , "INSERT INTO t2 VALUES (1), (2), (3)"
                      , "CREATE TABLE t3(a INT) ENGINE=InnoDB"
                      , "INSERT INTO t3 VALUES (1), (2), (3)"
                      , "CREATE TABLE t4_old(a INT) ENGINE=InnoDB"
                      , "INSERT INTO t4_old VALUES (1), (2), (3)"
                      ]
            retcode, result = self.execute_queries(queries, master_server)
            self.assertEqual(retcode, 0, msg=result)

            # take a backup
            cmd = [ xtrabackup
                  , "--defaults-file=%s" %master_server.cnf_file
                  , "--datadir=%s" %master_server.datadir
                  , "--backup"
                  , "--target-dir=%s" %backup_path
                  , '--debug-sync="data_copy_thread_func"'
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

            # Modify the original tables, then change space_ids by running DDL
            queries = [ "INSERT INTO t1 VALUES (4), (5), (6)"
                      , "DROP TABLE t1"
                      , "CREATE TABLE t1(a CHAR(1)) ENGINE=InnoDB"
                      , 'INSERT INTO t1 VALUES ("1"), ("2"), ("3")'
                      , "INSERT INTO t2 VALUES (4), (5), (6)"
                      , "ALTER TABLE t2 MODIFY a BIGINT"
                      , "INSERT INTO t2 VALUES (7), (8), (9)"
                      , "INSERT INTO t3 VALUES (4), (5), (6)"
                      , "TRUNCATE t3"
                      , "INSERT INTO t3 VALUES (7), (8), (9)"
                      , "INSERT INTO t4_old VALUES (4), (5), (6)"
                      , "ALTER TABLE t4_old RENAME t4"
                      , "INSERT INTO t4 VALUES (7), (8), (9)"
                      ]
            retcode, result = self.execute_queries(queries, master_server)
            self.assertEqual(retcode, 0, msg=result)

            #calculate_checksums
            orig_checksums = []
            for i in range(4):
                query = "CHECKSUM TABLE t%d" %(i+1)
                retcode, result = self.execute_query(query, master_server)
                self.assertEqual(retcode, 0, msg=result)
                orig_checksums.append(result)
           
            # Resume the xtrabackup process and remove the suspended file
            xtrabackup_subproc.send_signal(signal.SIGCONT)

            xtrabackup_subproc.wait()
            output_file.close()
            output_file = open(output_path,'r')
            output = ''.join(output_file.readlines())
            self.assertEqual(xtrabackup_subproc.returncode,0,msg="Xtrabackup: uh-oh >;)")

            # Clear our schema so we know the backup restored
            queries = [ "DROP SCHEMA test"
                      , "CREATE SCHEMA test"
                      ]
            retcode, result = self.execute_queries(queries,master_server)
            self.assertEqual(retcode, 0, result) 
        
            # prepare our main backup
            cmd = [ xtrabackup
                  , "--prepare"
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
            #retcode, output = self.execute_cmd(cmd, output_path, exec_path, True)
            #self.assertTrue(retcode==0,output)

            # stop our server
            master_server.stop()
           
            # remove some files
            clear_paths = [master_server.datadir
                          ,os.path.join(master_server.datadir,'test')
                          ]
            for clear_path in clear_paths:
                for file_name in os.listdir(clear_path):
                    if file_name.startswith('ib_logfile') or file_name == 'ibdata1' or file_name.endswith('.ibd'):
                        os.remove(os.path.join(clear_path,file_name))
        
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

            query = "SHOW TABLES IN test"
            retcode, result = self.execute_query(query, master_server)
            logging.test_debug("%s: %s" %(query, result)) 

            #calculate_checksums
            for i in range(4):
                query = "CHECKSUM TABLE t%d" %(i+1)
                retcode, result = self.execute_query(query, master_server)
                self.assertEqual(retcode, 0, msg=result)
                self.assertEqual(result, orig_checksums[i], msg = "%s:  %s || %s" %(query, orig_checksums[i], result))

