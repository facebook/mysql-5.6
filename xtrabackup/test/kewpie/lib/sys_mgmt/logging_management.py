#! /usr/bin/env python
# -*- mode: python; c-basic-offset: 2; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
#
# Copyright (C) 2009 Sun Microsystems
# Copyright (C) 2011 Patrick Crews
#
# Authors:
#
#  Jay Pipes <joinfu@sun.com>
#  Monty Taylor <mordred@sun.com>
#  Patrick Crews 
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
#
#
# This code is modified from the logging module used in the 
# drizzle-automation project - https://launchpad.net/drizzle-automation


""" Simple replacement for python logging module that doesn't suck """

import os
import time, sys


class loggingManager():
    """ Class to deal with logging
        We make a class just because we forsee ourselves being
        multi-threaded and it will be nice to have a single
        point of control for managing i/o.

        Also, this is the cleanest way I can think of to deal
        with test-reporting (again, multi-threaded and such

    """

    def __init__(self, variables):

        self.log_file = sys.stdout
        self.subunit_file = variables['subunitoutfile']
        # TODO - introduce an option to manually toggle
        # whether or not to reset or append to pre-existing file
        if os.path.exists(self.subunit_file):
            os.remove(self.subunit_file)
        self.report_fmt = '{0:<42} {1} {2:>8}'
        self.report_started = 0
        self.thick_line = '='*(80 - len("20110420-105402  "))
        self.thin_line = '-'*(80- len("20110420-105402  "))
        self.verbose_flag = variables['verbose']
        self.debug_flag = variables['debug']
        self.test_debug_flag = variables['testdebug']

    def _write_message(self,level, msg):
      self.log_file.write("%s %s %s\n" % (time.strftime("%Y%m%d-%H%M%S"), level, str(msg)))
      self.log_file.flush()

    def setOutput(self,file_name):
      if file_name == 'stdout':
        self.log_file= sys.stdout
      else:
        self.log_file= open(variables['log_file'],'w+')

    def info(self,msg):
      self._write_message("INFO", msg)

    def warning(self,msg):
      self._write_message("WARNING", msg)

    def error(self,msg):
      self._write_message("ERROR", msg)

    def verbose(self,msg):
      if self.verbose_flag:
          self._write_message("VERBOSE", msg)

    def debug(self,msg):
      if self.debug_flag:
          self._write_message("DEBUG", msg)

    def test_debug(self,msg):
        if self.test_debug_flag:
            self._write_message("TEST_DEBUG", msg)
 
    def debug_class(self,codeClass):
      if self.debug_flag:
        self._write_message("DEBUG**",codeClass)
        skip_keys = ['skip_keys', 'debug', 'verbose']
        for key, item in sorted(vars(codeClass).items()):
            if key not in codeClass.skip_keys and key not in skip_keys:
                self._write_message("DEBUG**",("%s: %s" %(key, item)))



    def test_report( self, test_name, test_status
                   , execution_time, additional_output = None
                   , report_output = False):
        """ We use this method to deal with writing out the test report

        """
        if not self.report_started:
            self.report_started = 1
            self.write_report_header()
        test_status = "[ %s ]" %(test_status)
        msg = self.report_fmt.format( test_name, test_status
                                    , execution_time)
        self._write_message("", msg)
        if additional_output and report_output:
            additional_output=additional_output.split('\n')
            for line in additional_output:
                line = line.strip()
                self._write_message("",line)

    def write_report_header(self):
        self.write_thick_line()
        self.test_report("TEST NAME", "RESULT", "TIME (ms)")
        self.write_thick_line()

    def write_thin_line(self):
        self._write_message("",self.thin_line)

    def write_thick_line(self):
        self._write_message("",self.thick_line)

    def subunit_start(self,test_name):
        """ We log a test being started for subunit output """
        with open(self.subunit_file,'a') as subunit_outfile:
            subunit_outfile.write("time: %sZ\n" %(time.strftime("%Y-%m-%d-%H:%M:%S")))
            subunit_outfile.write("test: %s\n" %(test_name))

    def subunit_stop(self, test_name, retcode, output):
        """ We log the return of a test appropriately:
            success, skip, failure, etc

        """
        result_map = {'pass':'success'
                     ,'fail':'failure'
                     }
        result = result_map[retcode]
        with open(self.subunit_file,'a') as subunit_outfile:
            subunit_outfile.write(time.strftime("time: %Y-%m-%d-%H:%M:%SZ\n"))
            if output:
                output_string = " [\n%s]\n" %(output)
            else:
                output_string = "\n" # we just want a newline if nothing here 
            subunit_outfile.write("%s: %s%s" %( result
                                              , test_name
                                              , output_string))
            
        
