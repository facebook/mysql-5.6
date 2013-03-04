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

def execute_sqlbench(test_cmd, test_executor, servers):
    """ Execute the commandline and return the result.
        We use subprocess as we can pass os.environ dicts and whatnot 

    """
    
    bot = test_executor
    sqlbench_outfile = os.path.join(bot.logdir,'sqlbench.out')
    sqlbench_output = open(sqlbench_outfile,'w')
    bot.logging.info("Executing sqlbench:  %s" %(test_cmd))
    bot.logging.info("This may take some time...")
    sqlbench_subproc = subprocess.Popen( test_cmd
                                       , shell=True
                                       , cwd=os.path.join(bot.system_manager.testdir, 'sql-bench')
                                       , env=bot.working_environment
                                       , stdout = sqlbench_output
                                       , stderr = subprocess.STDOUT
                                       )
    sqlbench_subproc.wait()
    retcode = sqlbench_subproc.returncode     

    sqlbench_output.close()
    sqlbench_file = open(sqlbench_outfile,'r')
    output = ''.join(sqlbench_file.readlines())
    sqlbench_file.close()

    bot.current_test_retcode = retcode
    bot.current_test_output = output
    test_status = process_sqlbench_output(bot)
    return test_status, retcode, output

def process_sqlbench_output(bot):
        
    # Check for 'Failed' in sql-bench output
    # The tests don't die on a failed test and
    # require some checking of the output file
    error_flag = False
    for inline in bot.current_test_output:
        if 'Failed' in inline:
            error_flag= True
            logging.info(inline.strip())
    if bot.current_test_retcode == 0 and not error_flag:
        return 'pass'
    else:
        return 'fail'

