#! /usr/bin/env python
# -*- mode: python; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
#
# Copyright (C) 2010 Patrick Crews
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

"""test_mode.py
   code for dealing with testing modes
   A given mode should have a systemInitializer, testManager, and testExecutor
   that define how to setup, manage, and execute test cases

"""

# imports
import sys

def handle_mode(variables, system_manager):
    """ Deals with the 'mode' option and returns
        the appropriate code objects for the test-runner to play with

    """

    test_mode = variables['mode'].strip()
    system_manager.logging.info("Using testing mode: %s" %test_mode)

    if test_mode == 'cleanup':
        # cleanup mode - we try to kill any servers whose pid's we detect
        # in our workdir.  Might extend to other things (file cleanup, etc)
        # at some later point
        system_manager.cleanup(exit=True)

    else: # we expect something from dbqp_modes
        supported_modes = [ 'dtr'
                          , 'randgen'
                          , 'sysbench'
                          , 'sqlbench'
                          , 'crashme'
                          , 'native'
                          ]
        if test_mode not in supported_modes:
            system_manager.logging.error("invalid mode argument: %s" %test_mode)
            sys.exit(1)
        
        mgmt_module = "lib.modes.%s.%s_test_management" %(test_mode, test_mode)
        tmp = __import__(mgmt_module, globals(), locals(), ['testManager'], -1)
        testManager = tmp.testManager

        exec_module = "%s.%s_test_execution" %(test_mode, test_mode)
        tmp = __import__(exec_module, globals(), locals(), ['testExecutor'], -1)
        testExecutor = tmp.testExecutor        

    test_manager = testManager( variables, system_manager )
    return (test_manager, testExecutor)

