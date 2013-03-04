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

"""system_management.py
   code for dealing with system-level 'stuff'.
   This includes setting environment variables, looking for clients,
   so on and so forth.

   These things are / should be constant regardless of the testing being done
   We do an initial preflight / setup, we then call the mode-specific
   system_initialise() to do whatever the testing mode requires to do that
   voodoo that it do so well

"""

# imports
import os
import sys
import shutil
import getpass
import commands

from uuid import uuid4

from lib.sys_mgmt.code_management import codeManager
from lib.sys_mgmt.environment_management import environmentManager
from lib.sys_mgmt.logging_management import loggingManager
from lib.sys_mgmt.port_management import portManager
from lib.sys_mgmt.time_management import timeManager

class systemManager:
    """Class to deal with the basics of system-level interaction
       and awareness

       Uses other managers to handle sub-tasks like port management

    """
    def __init__(self, variables, tree_type='drizzle'):
        self.logging = loggingManager(variables)
        if variables['verbose']:
            self.logging.verbose("Initializing system manager...")

        self.skip_keys = [ 'port_manager'
                         , 'logging'
                         , 'code_manager'
                         , 'env_manager'
                         , 'environment_reqs'
                         ]
        self.debug = variables['debug']
        self.verbose = variables['verbose']
        self.port_manager = portManager(self,variables['debug'])
        self.time_manager = timeManager(self)

        # environment manager handles updates / whatever to testing environments
        self.env_manager = environmentManager(self, variables)

        self.no_shm = variables['noshm']
        self.shm_path = self.find_path(["/dev/shm", "/tmp"], required=0)
        self.cur_os = os.uname()[0]
        self.cur_user = getpass.getuser()
        self.rootdir = variables['qp_root']
        self.workdir = os.path.abspath(variables['workdir'])
        self.testdir = os.path.abspath(variables['testdir'])
        self.datadir = os.path.join(self.rootdir,'qp_data')
        self.top_srcdir = os.path.abspath(variables['topsrcdir'])
        self.top_builddir = os.path.abspath(variables['topbuilddir'])
        self.start_dirty = variables['startdirty']
        self.valgrind = variables['valgrind']
        self.valgrind_suppress_file = variables['valgrindsuppressions']
        self.helgrind = variables['helgrind']
        # helgrind implies --valgrind
        if self.helgrind:
            self.valgrind=self.helgrind
        self.gdb = variables['gdb']
        self.manual_gdb = variables['manualgdb']
        self.randgen_path = variables['randgenpath']
        self.randgen_seed = variables['randgenseed']
        # there may be a better place to put this...
        self.innobackupex_path = variables['innobackupexpath']
        self.xtrabackup_path = variables['xtrabackuppath']
        self.tar4ibd_path = variables['tar4ibdpath']
        # We add tar4ibd to PATH if defined
        if self.tar4ibd_path:
            self.env_manager.set_env_var( 'PATH', self.env_manager.append_env_var( 'PATH'
                                                           , os.path.dirname(self.tar4ibd_path)
                                                           , suffix=0
                                                           ))

        self.wsrep_provider_path = variables['wsrepprovider']

        # we use this to preface commands in order to run valgrind and such
        self.cmd_prefix = '' 

        # We find or generate our id file
        # We use a uuid to identify the symlinked
        # workdirs.  That way, each installation
        # Will have a uuid/tmpfs workingdir
        # We store in a file so we know what
        # is ours
        self.uuid = self.get_uuid()
        self.symlink_name = 'qp_workdir_%s_%s' %(self.cur_user, self.uuid)

        # initialize our workdir
        self.process_workdir()
        
        # Some ENV vars are system-standard
        # We describe and set them here and now
        # The format is name: (value, append, suffix)
        self.environment_reqs = { 'UMASK':'0660'
                                , 'UMASK_DIR' : '0770'
                                , 'LC_ALL' : 'C'
                                , 'LC_CTYPE' : 'C'
                                , 'LC_COLLATE' : 'C'
                                , 'USE_RUNNING_SERVER' : "0"
                                , 'TOP_SRCDIR' : self.top_srcdir
                                , 'TOP_BUILDDIR' : self.top_builddir
                                , 'DRIZZLE_TEST_DIR' : self.testdir
                                , 'DTR_BUILD_THREAD' : "-69.5"
                                }
        # set the env vars we need
        # self.process_environment_reqs(self.environment_reqs)
        self.env_manager.update_environment_vars(self.environment_reqs)

        # use our code_manager to handle the various basedirs 
        # we have been passed
        self.code_manager = codeManager(self, variables)

        # check for libtool
        self.libtool = self.libtool_check()

        # See if we need to do any further processing for special
        # options like valgrind and gdb
        self.handle_additional_reqs(variables)
     
        self.logging.debug_class(self)


    def create_dirset(self, rootdir, dirset):
        """ We produce the set of directories defined in dirset
            dirset is a set of dictionaries like
            {'dirname': 'subdir'}
            or {'dirname': {'subdir':'subsubdir}}...

            We generally expect there to be only a single
            top-level key.  The intent is to produce a dirset
            rooted at key[0], with various subdirs under that
            subsequest dirsets should be handles in separate calls...

        """
        for dirname in dirset.keys():
            full_path = os.path.join(rootdir, dirname)
            subdirset = dirset[dirname]
            if type(subdirset) is str:
                self.create_symlink(subdirset,full_path)
            else:
                self.create_dir(full_path)        
                # dirset[dirname] is a new dictionary
                if subdirset is None:
                    {}
                else:
                    self.create_dirset(full_path,subdirset)

        return full_path    

    def get_uuid(self):
        """ We look to see if a uuid file exists
            If so, we use that to know where to work
            If not we produce one so future runs
            have a definitive id to use

        """

        uuid_file_name = os.path.join(self.datadir, 'uuid')
        if os.path.exists(uuid_file_name):
            uuid_file = open(uuid_file_name,'r')
            uuid = uuid_file.readline().strip()
            uuid_file.close()
        else:
            uuid = uuid4()
            uuid_file = open(uuid_file_name,'w')
            uuid_file.write(str(uuid))
            uuid_file.close()
        return uuid

    def process_workdir(self):
        """ We create our workdir, analyze relevant variables
            to see if we should/shouldn't symlink to shm
            We do nothing if we have --start-dirty

        """
        
        if os.path.exists(self.workdir):
            # our workdir already exists
            if self.start_dirty:
                self.logging.info("Using --start-dirty, not attempting to touch directories")
                return
            else:
                self.cleanup() # We crawl / try to kill any server pids we find
                self.remove_dir(self.workdir)
        self.allocate_workdir()
    

    def allocate_workdir(self):
        """ Create a workdir according to user-supplied specs """
        if self.no_shm:
            self.logging.info("Using --no-shm, will not link workdir to shm")
            self.create_dir(self.workdir, subdir=0)
        elif self.shm_path == None:
            self.logging.info("Could not find shared memory path for use.  Not linking workdir to shm")
            self.create_dir(self.workdir, subdir=0)
        else:
            shm_workdir = self.create_dir(os.path.join(self.shm_path, self.symlink_name))
            self.logging.info("Linking workdir %s to %s" %(self.workdir, shm_workdir))  
            self.create_symlink(shm_workdir, self.workdir)

    def create_dir(self, dirname, subdir =1 ):
        """ Create a directory.  If subdir = 1,
            then the new dir should be a subdir of
            self.workdir.  Else, we just create dirname,
            which should really be dirpath in this case

        """

        if subdir:
            full_path = os.path.join(self.workdir, dirname)
        else:
            full_path = dirname

        if os.path.exists(full_path):
            if self.start_dirty:
                return full_path
            else:
                shutil.rmtree(full_path)
            self.logging.debug("Creating directory: %s" %(dirname))   
        try:
            os.makedirs(full_path)
        except IOError, e:
            self.logging.error("Problem creating directory: %s" %(full_path))
            self.logging.error(e)
            sys.exit(1)
        return full_path

    def remove_dir(self, dirname, require_empty=0 ):
        """ Remove the directory in question.
            We assume we want to brute-force clean
            things.  If require_empty = 0, then
            the dir must be empty to remove it

        """
        self.logging.debug("Removing directory: %s" %(dirname))
        if os.path.islink(dirname):
            os.remove(dirname)
        elif require_empty:
            os.rmdir(dirname)
        else:
            shutil.rmtree(dirname)

    def copy_dir(self, srcdir, tgtdir, overwrite = 1):
        """ Copy the contents of srcdir to tgtdir.
            We overwrite (remove/recreate) tgtdir
            if overwrite == 1

        """

        self.logging.debug("Copying directory: %s to %s" %(srcdir, tgtdir))
        if os.path.exists(tgtdir):
            if overwrite:
                self.remove_dir(tgtdir)
            else:
                self.logging.error("Cannot overwrite existing directory: %s" %(tgtdir))
                sys.exit(1)
        shutil.copytree(srcdir, tgtdir, symlinks=True)

    def create_symlink(self, source, link_name):
        """ We create a symlink to source named link_name """

        self.logging.debug("Creating symlink from %s to %s" %(source, link_name))
        if os.path.exists(link_name) or os.path.islink(link_name):
            os.remove(link_name)
        return os.symlink(source, link_name)

    def create_symlinks(self, needed_symlinks):
        """ We created the symlinks in needed_symlinks 
            We expect it to be tuples in source, link_name format

        """
        
        for needed_symlink in needed_symlinks:
            source, link_name = needed_symlink
            self.create_symlink(source, link_name)



    def find_path(self, paths, required=1):
        """We search for the files we need / want to be aware of
           such as the drizzled binary, the various client binaries, etc
           We use the required switch to determine if we die or not
           if we can't find the file.

           We expect paths to be a list of paths, ordered in terms
           of preference (ie we want to use something from search-path1
           before search-path2).

           We return None if no match found and this wasn't required

        """

        for test_path in paths:
            self.logging.debug("Searching for path: %s" %(test_path))
            if os.path.exists(test_path):
                return test_path
        if required:
            self.logging.error("Required file not found out of options: %s" %(" ,".join(paths)))
            sys.exit(1)
        else:
            return None

    def execute_cmd(self, cmd, must_pass = 1):
        """ Utility function to execute a command and
            return the output and retcode

        """

        self.logging.debug("Executing command: %s" %(cmd))
        (retcode, output)= commands.getstatusoutput(cmd)
        if not retcode == 0 and must_pass:
            self.logging.error("Command %s failed with retcode %d" %(cmd, retcode))
            self.logging.error("%s" %(output))
            sys.exit(1)
        return retcode, output

    def libtool_check(self):
        """ We search for libtool """
        libtool_path = '../libtool'
        if os.path.exists(libtool_path) and os.access( libtool_path
                                                     , os.X_OK):
            if self.valgrind or self.gdb:
                self.logging.info("Using libtool when running valgrind or debugger")
            return libtool_path
        else:
            return None

    def handle_additional_reqs(self, variables):
        """ Do what we need to do to set things up for
            options like valgrind and gdb

        """

        # do we need to setup for valgrind?
        if self.valgrind:
            if self.helgrind:
               valgrind_mode='helgrind'
            else:
                valgrind_mode='valgrind'
            self.handle_valgrind_reqs(variables['valgrindarglist'], mode=valgrind_mode)

    def handle_gdb_reqs(self, server, server_args):
        """ We generate the gdb init file and whatnot so we
            can run gdb properly

            if the user has specified manual-gdb, we provide
            them with a message about when to start and
            signal the server manager to simply wait
            for the server to be started by the user

        """
        extra_args = ''
        gdb_term_cmd = "xterm -title %s.%s " %( server.owner
                                              , server.name
                                              )
        gdb_file_name = "%s.gdbinit" %(server.name)

        if self.cur_os == 'Darwin': # Mac...ick ; P
            extra_args = [ "set env DYLD_INSERT_LIBRARIES /usr/lib/libgmalloc.dylib"
                         , "set env MallocStackLogging 1"
                         , "set env MallocScribble 1"
                         , "set env MallocPreScribble 1"
                         , "set env MallocStackLogging 1"
                         , "set env MallocStackLoggingNoCompact 1"
                         , "set env MallocGuardEdges 1"
                         ] 

        # produce our init file
        if extra_args:
            extra_args = "\n".join(extra_args)
        gdb_file_contents = [ "set args %s" %(" ".join(server_args))
                            , "%s" % (extra_args)
                            , "set breakpoint pending on"
	                          , "break drizzled::parse"
	                          , "commands 1"
                            , "disable 1"
	                          , "end"
                            , "set breakpoint pending off"
	                          , "run"
                            ]
        gdb_file_path = os.path.join(server.tmpdir, gdb_file_name)
        gdb_init_file = open(gdb_file_path,'w')
        gdb_init_file.write("\n".join(gdb_file_contents))
        gdb_init_file.close()

        # return our command line
        if self.libtool:
            libtool_string = "%s --mode=execute " %(self.libtool)
        else:
            libtool_string = ""

        if self.manual_gdb:
            self.logging.info("To start gdb, open another terminal and enter:")
            self.logging.info("%s/../libtool --mode=execute gdb -cd %s -x %s %s" %( server.code_tree.testdir
                                                                                  , server.code_tree.testdir
                                                                                  , gdb_file_path
                                                                                  , server.server_path
                                                                                  ) )
            return None

        else:
            return "%s -e %s gdb -x %s %s" %( gdb_term_cmd
                                            , libtool_string
                                            , gdb_file_path
                                            , server.server_path
                                            )

    def handle_valgrind_reqs(self, optional_args, mode='valgrind'):
        """ We do what voodoo we need to do to run valgrind """
        valgrind_args = [ "--show-reachable=yes"
                        , "--malloc-fill=22"
                        , "--free-fill=22"
                        # , "--trace-children=yes" this is for callgrind only
                        ]
        if optional_args:
        # we override the defaults with user-specified options
            valgrind_args = optional_args
        self.logging.info("Running valgrind with options: %s" %(" ".join(valgrind_args)))

        # set our environment variable
        self.env_manager.set_env_var('VALGRIND_RUN', '1', quiet=0)

        # generate command prefix to call valgrind
        cmd_prefix = ''
        if self.libtool:
            cmd_prefix = "%s --mode=execute valgrind " %(self.libtool)
        if mode == 'valgrind':
            # default mode
            args = [ "--tool=memcheck"
                   , "--leak-check=yes"
                   , "--num-callers=16" 
                   ]
        elif mode == 'helgrind':
            args = [ "--tool=helgrind"
                   , "--num-callers=16"
                   ]
            # look for our suppressions file and add it to the mix if found
            if os.path.exists(self.valgrind_suppress_file):
                args = args + [ "--suppressions=%s" %(suppress_file) ]

            cmd_prefix = cmd_prefix + " ".join(args + valgrind_args)
        self.cmd_prefix = cmd_prefix  
        
        # add debug libraries to ld_library_path
        debug_path = '/usr/lib/debug'
        if os.path.exists(debug_path):
            self.env_manager.append_env_var("LD_LIBRARY_PATH", debug_path, suffix=1)
            self.env_manager.append_env_var("DYLD_LIBRARY_PATH", debug_path, suffix=1)


    def cleanup(self, exit=False):
        """ We try to kill any servers whose pid files
            we detect lurking about

        """
        
        self.pid_hunt_and_kill(exit)

    def pid_hunt_and_kill(self, exit):
        """ Crawl our workdir and look for server.pid files
            We read 'em and kill 'em if we find 'em

        """

        for root, dirs, files in os.walk(self.workdir):
            #print root, dirs, files
            for found_file in files:
                if found_file.endswith('.pid'):
                    file_path = os.path.join(root, found_file)
                    pid_file = open(file_path,'r')
                    pid = pid_file.readline().strip()
                    pid_file.close()
                    self.logging.info("Killing pid %s from %s" %( pid
                                                                , file_path
                                                                ))
                    self.kill_pid(pid)
        if exit:
            sys.exit(0)

    def find_pid(self, pid):
        """ Execute ps and see if we find the pid """

        try:
            os.kill(int(pid),0)
        except OSError:
            return False
        else:
            return True

    def kill_pid(self, pid):
        """ We kill the specified pid """
        try:
            os.kill(int(pid),9)
        except OSError:
            self.logging.verbose("PID: %s was not running despite the existence of a pid file.  This may be of note...")
        return
   
    def get_ip_address(self):
        """ We find the ip address of our host.
            Currently a bit hacky
       
        """

        ip_address = '127.0.0.1' 
        retcode, output = self.execute_cmd("ifconfig") 
        for line in output.split('\n'):
            line = line.strip()
            if line.startswith('inet addr'):
                ip_address = line.split(':')[1].split(' ')[0].strip()
                if ip_address != '127.0.0.1':
                    return ip_address
        return ip_address
 
        
        
        
