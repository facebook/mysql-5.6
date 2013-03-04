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

"""code_management.py
   code for handling awareness / validation / management of
   server source code.  We want to be able to provide dbqp
   with an array of basedir values and have tests execute appropriately.

   This includes allowing for advanced testing modes that utilize combos
   of the available servers, etc.

"""

# imports
import os
import sys

class codeManager:
    """ Class that handles / provides the various server sources that
        may be provided to dbqp

    """

    def __init__(self, system_manager, variables):
        self.system_manager = system_manager
        self.logging = self.system_manager.logging
        self.code_trees = {} # we store type: codeTree
        self.type_delimiter = ':type:'

        # We go through the various --basedir values provided
        provided_basedirs = variables['basedir']
        first = True
        for basedir in provided_basedirs:
            # We initialize a codeTree object
            # and store some information about it
            code_type, code_tree = self.process_codeTree(basedir, variables)
            if first:
                self.test_tree = code_tree #first basedir = type under test
                self.test_type = code_type
                first = False
            self.add_codeTree(code_type, code_tree)

    def add_codeTree(self, code_type, code_tree):
        # We add the codeTree to a list under 'type'
        # (mysql, drizzle, etc)
        # This organization may need to change at some point
        # but will work for now
        if code_type not in self.code_trees:
            self.code_trees[code_type] = []
        self.code_trees[code_type].append(code_tree)

    def process_codeTree(self, basedir, variables, code_type=None):
        """Import the appropriate module depending on the type of tree
           we are testing. 

           Drizzle is the only supported type currently

        """

        self.logging.verbose("Processing code rooted at basedir: %s..." %(basedir))
        # We comment out / remove the old get_code_type method
        # as the expectation is that the type will be passed as part of the 
        # basedir string / will be the default type (which will be configurable)

        #code_type = self.get_code_type(basedir)
        if basedir.find(self.type_delimiter) != -1:
            basedir, code_type = basedir.split(self.type_delimiter)
        elif code_type:
            code_type = code_type
        else:
            code_type = variables['defaultservertype']
        if code_type == 'drizzle':
            # base_case
            from lib.sys_mgmt.codeTree import drizzleTree
            test_tree = drizzleTree(basedir,variables,self.system_manager)
            return code_type, test_tree
        elif code_type == 'mysql':
            from lib.sys_mgmt.codeTree import mysqlTree
            test_tree = mysqlTree(basedir,variables,self.system_manager)
            return code_type, test_tree
        elif code_type == 'galera':
            from lib.sys_mgmt.codeTree import galeraTree
            test_tree = galeraTree(basedir, variables, self.system_manager)
            return code_type, test_tree
        elif code_type == 'percona':
            from lib.sys_mgmt.codeTree import perconaTree
            test_tree = perconaTree(basedir,variables,self.system_manager)
            return code_type, test_tree
        else:
            self.logging.error("Tree_type: %s not supported yet" %(tree_type))
            sys.exit(1)        

    def get_tree(self, server_type, server_version):
        """ We return an appropriate server tree for use in testing """
   
        # We can make this more robust later on
        return self.code_trees[server_type][0]
        
            
