#! /usr/bin/env python
# -*- mode: python; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
#
# Copyright (C) 2011 Patrick Crews
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

"""environment_management

   code we use to tweak the environment during test execution

"""

# imports
import os
import sys
import copy

class environmentManager():
    """ Handles updating / managing environment stuff """
    def __init__(self, system_manager, variables):
        self.system_manager = system_manager
        self.logging = self.system_manager.logging
        self.env_var_delimiter = ':'
        
    def join_env_var_values(self, value_list):
        """ Utility to join multiple values into a nice string
            for setting an env var to
 
        """

        return self.env_var_delimiter.join(value_list)

    def set_env_var(self, var_name, var_value, quiet=0):
        """Set an environment variable.  We really just abstract
           voodoo on os.environ

        """
        if not quiet:
            self.logging.debug("Setting env var: %s" %(var_name))
        try:
            os.environ[var_name]=var_value
        except Exception, e:
            self.logging.error("Issue setting environment variable %s to value %s" %(var_name, var_value))
            self.logging.error("%s" %(e))
            sys.exit(1)

    def update_environment_vars(self, desired_vars, working_environment=None):
        """ We update the environment vars with desired_vars
            The expectation is that you know what you are asking for ; )
            If working_environment is provided, we will update that with
            desired_vars.  We operate directly on os.environ by default
            We return our updated environ dictionary

        """

        if not working_environment:
            working_environment = os.environ
        working_environment.update(desired_vars)
        return working_environment

    def create_working_environment(self, desired_vars):
        """ We return a copy of os.environ updated with desired_vars """

        working_copy = copy.deepcopy(os.environ)
        return self.update_environment_vars( desired_vars
                                    , working_environment = working_copy )

    def append_env_var(self, var_name, append_string, suffix=1, quiet=0):
        """ We add the values in var_values to the environment variable 
            var_name.  Depending on suffix value, we either append or prepend
            we return a string suitable for os.putenv

        """
        new_var_value = ""
        if var_name in os.environ:
            cur_var_value = os.environ[var_name]
            if suffix: # We add new values to end of existing value
                new_var_values = [ cur_var_value, append_string ]
            else:
                new_var_values = [ append_string, cur_var_value ]
            new_var_value = self.env_var_delimiter.join(new_var_values)
        else:
            # No existing variable value
            new_var_value = append_string
        return new_var_value
