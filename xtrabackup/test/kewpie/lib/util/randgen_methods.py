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
import subprocess

def execute_randgen(test_cmd, test_executor, servers, schema='test'):
    randgen_outfile = os.path.join(test_executor.logdir,'randgen.out')
    randgen_output = open(randgen_outfile,'w')
    server_type = test_executor.master_server.type
    if server_type in ['percona','galera']:
        # it is mysql for dbd::perl purposes
        server_type = 'mysql'
    dsn = "--dsn=dbi:%s:host=127.0.0.1:port=%d:user=root:password="":database=%s" %( server_type
                                                                                     , servers[0].master_port
                                                                                     , schema)
    randgen_cmd = " ".join([test_cmd, dsn])
    randgen_subproc = subprocess.Popen( randgen_cmd
                                      , shell=True
                                      , cwd=test_executor.system_manager.randgen_path
                                      , env=test_executor.working_environment
                                      , stdout = randgen_output
                                      , stderr = subprocess.STDOUT
                                      )
    randgen_subproc.wait()
    retcode = randgen_subproc.returncode     
    randgen_output.close()

    randgen_file = open(randgen_outfile,'r')
    output = ''.join(randgen_file.readlines())
    randgen_file.close()
    if retcode == 0:
        if not test_executor.verbose:
            output = None
    return retcode, output


