#! /usr/bin/env python
# -*- mode: python; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
#
# Copyright (C) 2010, 2011 Patrick Crews
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

"""codeTree

   definition of what a code tree should look like
   to the test-runner (which files /directories it should find where)
   
   Files paths can be in one of several locations that we locate via
   systemManager methods

"""
# imports
import os
import sys
from ConfigParser import RawConfigParser


class codeTree(object):
    """ Defines what files / directories we should find and where
        allows for optional / required.

    """
  
    def __init__(self, basedir, variables, system_manager):
        self.debug = variables['debug']
        self.system_manager = system_manager
        self.logging = system_manager.logging
        self.basedir = self.system_manager.find_path(os.path.abspath(basedir))

    def debug_status(self):
            self.logging.debug(self)
            for key, item in sorted(vars(self).items()):
                self.logging.debug("%s: %s" %(key, item))

class drizzleTree(codeTree):
    """ What a Drizzle code tree should look like to the test-runner
    
    """

    def __init__(self, basedir, variables,system_manager):
        self.system_manager = system_manager
        self.logging = self.system_manager.logging
        self.skip_keys = ['ld_lib_paths']
        self.debug = variables['debug']
        self.verbose = variables['verbose']
        self.version_checked = False
        self.innodb_version = None
        self.xtradb_version = None
        self.basedir = self.system_manager.find_path([os.path.abspath(basedir)])
        self.source_dist = os.path.isdir(os.path.join(self.basedir, 'drizzled'))
        self.builddir = self.system_manager.find_path([os.path.abspath(self.basedir)])
        self.top_builddir = variables['topbuilddir']
        self.testdir = self.system_manager.find_path([os.path.abspath(variables['testdir'])])
        self.clientbindir = self.system_manager.find_path([os.path.join(self.builddir, 'client')
                                     , os.path.join(self.basedir, 'client')
                                     , os.path.join(self.basedir, 'bin')])
        self.srcdir = self.system_manager.find_path([self.basedir])
        self.suite_paths = variables['suitepaths']


        self.drizzle_client = self.system_manager.find_path([os.path.join(self.clientbindir,
                                                     'drizzle')])

        self.drizzledump = self.system_manager.find_path([os.path.join(self.clientbindir,
                                                     'drizzledump')])

        self.drizzleimport = self.system_manager.find_path([os.path.join(self.clientbindir,
                                                     'drizzleimport')])

        self.drizzle_server = self.system_manager.find_path([os.path.join(self.basedir,'drizzled/drizzled'),
                                         os.path.join(self.clientbindir,'drizzled'),
                                         os.path.join(self.basedir,'libexec/drizzled'),
                                         os.path.join(self.basedir,'bin/drizzled'),
                                         os.path.join(self.basedir,'sbin/drizzled'),
                                         os.path.join(self.builddir,'drizzled/drizzled')])


        self.drizzleslap = self.system_manager.find_path([os.path.join(self.clientbindir,
                                                     'drizzleslap')])

        self.schemawriter = self.system_manager.find_path([os.path.join(self.basedir,
                                                     'drizzled/message/schema_writer'),
                                        os.path.join(self.builddir,
                                                     'drizzled/message/schema_writer')])

        self.drizzletest = self.system_manager.find_path([os.path.join(self.clientbindir,
                                                   'drizzletest')])

        self.trx_reader = self.system_manager.find_path([os.path.join(self.basedir,
                                                                 'plugin/transaction_log/utilities/drizzletrx')])

        self.server_version_string = None
        self.server_executable = None
        self.server_version = None
        self.server_compile_os = None
        self.server_platform = None
        self.server_compile_comment = None
        self.type = 'drizzle'

        self.process_server_version()
        self.ld_lib_paths = self.get_ld_lib_paths()
         
        self.report()

        self.logging.debug_class(self)

    def report(self):
        self.logging.info("Using Drizzle source tree:")
        report_keys = ['basedir'
                      ,'clientbindir'
                      ,'testdir'
                      ,'server_version'
                      ,'server_compile_os'
                      ,'server_platform'
                      ,'server_comment']
        for key in report_keys:
            self.logging.info("%s: %s" %(key, vars(self)[key]))
        


    def process_server_version(self):
        """ Get the server version number from the found server executable """
        (retcode, self.server_version_string) = self.system_manager.execute_cmd(("%s --no-defaults --version" %(self.drizzle_server)))
        # This is a bit bobo, but we're doing it, so nyah
        # TODO fix this : )
        self.server_executable, data_string = [data_item.strip() for data_item in self.server_version_string.split('Ver ')]
        self.server_version, data_string = [data_item.strip() for data_item in data_string.split('for ')]
        self.server_compile_os, data_string = [data_item.strip() for data_item in data_string.split(' on')]
        self.server_platform = data_string.split(' ')[0].strip()
        self.server_comment = data_string.replace(self.server_platform,'').strip()

    def get_ld_lib_paths(self):
        """ Return a list of paths we want added to LD_LIB variables

            These are processed later at the server_manager level, but we want to 
            specify them here (for a drizzle source tree) and now
  
        """
        ld_lib_paths = []
        if self.source_dist:
            ld_lib_paths = [ os.path.join(self.basedir,"libdrizzleclient/.libs/")
                           #, os.path.join(self.basedir,"libdrizzle-2.0/libdrizzle.libs")
                           , os.path.join(self.basedir,"libdrizzle/.libs")
                           , os.path.join(self.basedir,"libdrizzle-2.0/libdrizzle/.libs")
                           , os.path.join(self.basedir,"libdrizzle-1.0/libdrizzle/.libs")
                           , os.path.join(self.basedir,"mysys/.libs/")
                           , os.path.join(self.basedir,"mystrings/.libs/")
                           , os.path.join(self.basedir,"drizzled/.libs/")
			                     , "/usr/local/lib"
                           ]
        else:
            ld_lib_paths = [ os.path.join(self.basedir,"lib")]
        return ld_lib_paths


class mysqlTree(codeTree):
    """ What a MySQL code tree should look like to the test-runner
    
    """

    def __init__(self, basedir, variables,system_manager):
        self.system_manager = system_manager
        self.logging = self.system_manager.logging
        self.skip_keys = ['ld_lib_paths']
        self.debug = variables['debug']
        self.verbose = variables['verbose']
        self.version_checked = False
        self.innodb_version = None
        self.xtradb_version = None
        self.basedir = self.system_manager.find_path([os.path.abspath(basedir)])
        self.source_dist = os.path.isdir(os.path.join(self.basedir, 'mysqld'))
        self.builddir = self.system_manager.find_path([os.path.abspath(self.basedir)])
        self.top_builddir = variables['topbuilddir']
        self.testdir = self.system_manager.find_path([os.path.abspath(variables['testdir'])])
        self.clientbindir = self.system_manager.find_path([os.path.join(self.basedir, 'client_release')
                                                         , os.path.join(self.basedir, 'client_debug')
                                                         , os.path.join(self.basedir, 'client')
                                                         , os.path.join(self.basedir, 'bin')])
        self.charsetdir = self.system_manager.find_path([os.path.join(self.basedir, 'mysql/charsets')
                                                       , os.path.join(self.basedir, 'sql/share/charsets')
                                                       , os.path.join(self.basedir, 'share/charsets')
                                                       , os.path.join(self.basedir, 'share/mysql/charsets')
                                                        ])
        self.langdir = self.system_manager.find_path([os.path.join(self.basedir, 'share/mysql')
                                                    , os.path.join(self.basedir, 'sql/share')
                                                    , os.path.join(self.basedir, 'share')])


        self.srcdir = self.system_manager.find_path([self.basedir])
        self.suite_paths = variables['suitepaths']

        self.mysql_client = self.system_manager.find_path([os.path.join( self.clientbindir
                                                                       , 'mysql')])

        self.mysqldump = self.system_manager.find_path([os.path.join( self.clientbindir
                                                                    , 'mysqldump')])

        self.mysqlcheck = self.system_manager.find_path([os.path.join( self.clientbindir
                                                                    , 'mysqlcheck')])


        self.mysqlimport = self.system_manager.find_path([os.path.join( self.clientbindir
                                                                      , 'mysqlimport')])

        self.mysqladmin = self.system_manager.find_path([os.path.join( self.clientbindir
                                                                     , 'mysqladmin')])
        self.mysql_upgrade = self.system_manager.find_path([os.path.join( self.clientbindir
                                                                       , 'mysql_upgrade')])
        self.mysql_server = self.system_manager.find_path([ os.path.join(self.basedir, 'sql/mysqld-debug')
                                                          , os.path.join(self.basedir, 'libexec/mysqld-debug')
                                                          , os.path.join(self.basedir, 'sbin/mysqld-debug')
                                                          , os.path.join(self.basedir, 'bin/mysqld-debug')
                                                          , os.path.join(self.basedir, 'sql/mysqld')
                                                          , os.path.join(self.basedir, 'libexec/mysqld')
                                                          , os.path.join(self.basedir, 'sbin/mysqld')
                                                          , os.path.join(self.basedir, 'bin/mysqld')
                                                          , os.path.join(self.basedir, 'sql/mysqld-max-nt')
                                                          , os.path.join(self.basedir, 'libexec/mysqld-max-nt')
                                                          , os.path.join(self.basedir, 'sbin/mysqld-max-nt')
                                                          , os.path.join(self.basedir, 'bin/mysqld-max-nt')
                                                          , os.path.join(self.basedir, 'sql/mysqld-max')
                                                          , os.path.join(self.basedir, 'libexec/mysqld-max')
                                                          , os.path.join(self.basedir, 'sbin/mysqld-max')
                                                          , os.path.join(self.basedir, 'bin/mysqld-max')
                                                          , os.path.join(self.basedir, 'sql/mysqld-nt')
                                                          , os.path.join(self.basedir, 'libexec/mysqld-nt')
                                                          , os.path.join(self.basedir, 'sbin/mysqld-nt')
                                                          , os.path.join(self.basedir, 'bin/mysqld-nt')
                                                          ])



        self.mysqlslap = self.system_manager.find_path([os.path.join(self.clientbindir,
                                                     'mysqlslap')], required=0)

        self.mysqltest = self.system_manager.find_path([os.path.join(self.clientbindir,
                                                   'mysqltest')])
        self.server_version_string = None
        self.server_executable = None
        self.server_version = None
        self.server_compile_os = None
        self.server_platform = None
        self.server_compile_comment = None
        self.type = 'mysql'
        self.process_server_version()
        self.ld_lib_paths = self.get_ld_lib_paths()
        self.bootstrap_path = os.path.join( self.system_manager.workdir
                                          , 'mysql_bootstrap.sql' )
        # we produce the bootstrap file that is used to set up a 'fresh'
        # mysql server
        self.generate_bootstrap()
        # we produce the baseline config that will be used / added
        # to when we allocate servers for tests
        # self.generate_config_template()
        self.report()

        self.logging.debug_class(self)

    def report(self):
        self.logging.info("Using mysql source tree:")
        report_keys = ['basedir'
                      ,'clientbindir'
                      ,'testdir'
                      ,'server_version'
                      ,'server_compile_os'
                      ,'server_platform'
                      ,'server_comment']
        for key in report_keys:
            self.logging.info("%s: %s" %(key, vars(self)[key]))
        
    def process_server_version(self):
        """ Get the server version number from the found server executable """
        (retcode, self.server_version_string) = self.system_manager.execute_cmd(("%s --no-defaults --version" %(self.mysql_server)))
        # This is a bit bobo, but we're doing it, so nyah
        # TODO fix this : )
        self.server_executable, data_string = [data_item.strip() for data_item in self.server_version_string.split('Ver ')]
        self.server_version, data_string = [data_item.strip() for data_item in data_string.split('for ')]
        self.server_compile_os, data_string = [data_item.strip() for data_item in data_string.split(' on')]
        self.server_platform = data_string.split(' ')[0].strip()
        self.server_comment = data_string.replace(self.server_platform,'').strip()

    def get_ld_lib_paths(self):
        """ Return a list of paths we want added to LD_LIB variables

            These are processed later at the server_manager level, but we want to 
            specify them here (for a mysql source tree) and now
  
        """
        ld_lib_paths = []
        if self.source_dist:
            ld_lib_paths = [ os.path.join(self.basedir,"libmysql/.libs/")
                           , os.path.join(self.basedir,"libmysql_r/.libs")
                           , os.path.join(self.basedir,"zlib/.libs")
                           ]
        else:
            ld_lib_paths = [ os.path.join(self.basedir,"lib")
                           , os.path.join(self.basedir,"lib/mysql")]
        return ld_lib_paths

    def generate_bootstrap(self):
        """ We do the voodoo that we need to in order to create the bootstrap
            file needed by MySQL

        """
        found_new_sql = False
        # determine if we have a proper area for our sql or if we
        # use the rigged method from 5.0 / 5.1
        # first we search various possible locations
        test_file = "mysql_system_tables.sql"
        for candidate_dir in [ "mysql"
                         , "sql/share"
                         , "share/mysql"
			                   , "share"
                         , "scripts"]:
            candidate_path = os.path.join(self.basedir, candidate_dir, test_file)
            if os.path.exists(candidate_path):
                bootstrap_file = open(self.bootstrap_path,'w')
                bootstrap_file.write("use mysql\n")
                for sql_file in [ 'mysql_system_tables.sql' #official mysql system tables
                                , 'mysql_system_tables_data.sql' #initial data for sys tables
                                , 'mysql_test_data_timezone.sql' # subset of full tz table data for testing
                                , 'fill_help_tables.sql' # fill help tables populated only w/ src dist(?)
                                ]:
                    sql_file_path = os.path.join(self.basedir,candidate_dir,sql_file)
                    sql_file_handle = open(sql_file_path,'r')
                    for line in sql_file_handle.readlines():
                        bootstrap_file.write(line)
                    sql_file_handle.close()
                found_new_sql = True
                break
        if not found_new_sql:
            # Install the system db's from init_db.sql
            # that is in early 5.1 and 5.0 versions of MySQL
            sql_file_path = os.path.join(self.basedir,'mysql-test/lib/init_db.sql')
            self.logging.info("Attempting to use bootstrap file - %s" %(sql_file_path))
            try:
                in_file = open(sql_file_path,'r')
                bootstrap_file = open(self.bootstrap_path,'w')
                for line in in_file.readlines():
                    bootstrap_file.write(line)
                in_file.close()
            except IOError:
                self.logging.error("Cannot find data for generating bootstrap file")
                self.logging.error("Cannot proceed without this, system exiting...")
                sys.exit(1)
        # Remove anonymous users
        bootstrap_file.write("DELETE FROM mysql.user where user= '';\n")
        # Create mtr database
        """
        bootstrap_file.write("CREATE DATABASE mtr;\n")
        for sql_file in [ 'mtr_warnings.sql' # help tables + data for warning detection / suppression
                        , 'mtr_check.sql' # Procs for checking proper restore post-testcase
                        ]:
            sql_file_path = os.path.join(self.basedir,'mysql-test/include',sql_file)
            sql_file_handle = open(sql_file_path,'r')
            for line in sql_file_handle.readlines():
                bootstrap_file.write(line)
            sql_file_handle.close()
        """
        bootstrap_file.close()
        return

class galeraTree(mysqlTree):
    """ What a Galera-using tree looks like.
        This is essentially the same as a MySQL tree, but we
        want to tweak bootstrap generation here to skip PERFORMANCE_SCHEMA
        Additionally, we may wish to scan a basedir / codeTree for
        Galera-specific components in the future

    """

    def __init__( self, basedir, variables, system_manager):
        super(galeraTree, self).__init__( basedir, variables, system_manager)
        self.type= 'galera'
        self.wsrep_sst_mysqldump = self.system_manager.find_path([ os.path.join(self.basedir, 'scripts/wsrep_sst_mysqldump')
                                                          , os.path.join(self.basedir, 'sbin/wsrep_sst_mysqldump')
                                                          , os.path.join(self.basedir, 'bin/wsrep_sst_mysqldump')
                                                          ])
        # add wsrep_sst_* scripts and mysqldump / mysql clients to PATH
        env_manager = self.system_manager.env_manager
        for file_name in (self.wsrep_sst_mysqldump, self.mysqldump, self.mysql_client):
            file_path = os.path.dirname(file_name)
            env_manager.set_env_var( 'PATH', env_manager.append_env_var( 'PATH'
                                                           , file_path, suffix=0
                                                           ))
 

    def generate_bootstrap(self):
        """ We do the voodoo that we need to in order to create the bootstrap
            file needed by MySQL

        """
        found_new_sql = False
        # determine if we have a proper area for our sql or if we
        # use the rigged method from 5.0 / 5.1
        # first we search various possible locations
        test_file = "mysql_system_tables.sql"
        for candidate_dir in [ "mysql"
                         , "sql/share"
                         , "share/mysql"
			                   , "share"
                         , "scripts"]:
            candidate_path = os.path.join(self.basedir, candidate_dir, test_file)
            if os.path.exists(candidate_path):
                bootstrap_file = open(self.bootstrap_path,'w')
                bootstrap_file.write("use mysql\n")
                for sql_file in [ 'mysql_system_tables.sql' #official mysql system tables
                                , 'mysql_system_tables_data.sql' #initial data for sys tables
                                , 'mysql_test_data_timezone.sql' # subset of full tz table data for testing
                                , 'fill_help_tables.sql' # fill help tables populated only w/ src dist(?)
                                ]:
                    sql_file_path = os.path.join(self.basedir,candidate_dir,sql_file)
                    sql_file_handle = open(sql_file_path,'r')
                    should_write = True
                    for line in sql_file_handle.readlines():
                        if line.startswith("-- PERFORMANCE SCHEMA INSTALLATION"):
                            # We skip performance schema for now to avoid issues
                            # with the mysqldump sst method
                            should_write = False
                        if 'proxies_priv' in line and not should_write:
                            should_write = True
                        if should_write:
                            bootstrap_file.write(line)
                    sql_file_handle.close()
                found_new_sql = True
                break
        if not found_new_sql:
            # Install the system db's from init_db.sql
            # that is in early 5.1 and 5.0 versions of MySQL
            sql_file_path = os.path.join(self.basedir,'mysql-test/lib/init_db.sql')
            self.logging.info("Attempting to use bootstrap file - %s" %(sql_file_path))
            try:
                in_file = open(sql_file_path,'r')
                bootstrap_file = open(self.bootstrap_path,'w')
                for line in in_file.readlines():
                    bootstrap_file.write(line)
                in_file.close()
            except IOError:
                self.logging.error("Cannot find data for generating bootstrap file")
                self.logging.error("Cannot proceed without this, system exiting...")
                sys.exit(1)
        # Remove anonymous users
        bootstrap_file.write("DELETE FROM mysql.user where user= '';\n")
        # Create mtr database
        """
        bootstrap_file.write("CREATE DATABASE mtr;\n")
        for sql_file in [ 'mtr_warnings.sql' # help tables + data for warning detection / suppression
                        , 'mtr_check.sql' # Procs for checking proper restore post-testcase
                        ]:
            sql_file_path = os.path.join(self.basedir,'mysql-test/include',sql_file)
            sql_file_handle = open(sql_file_path,'r')
            for line in sql_file_handle.readlines():
                bootstrap_file.write(line)
            sql_file_handle.close()
        """
        bootstrap_file.close()
        return
        



class perconaTree(mysqlTree):
    """ What a Percona code tree should look like to the test-runner
        This should essentially be the same as a MySQL tree, but with a 
        distinct server.type + any other goodies that differentiate
     
    """
    
    def __init__(self, basedir, variables,system_manager):
        super(perconaTree, self).__init__( basedir, variables, system_manager)
        self.type='percona'
