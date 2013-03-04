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

"""port_management.py
   code for dealing with the various tasks
   around handing out and managing server ports
   that we need to run tests

"""

# imports
import os
import sys

class portManager:
    """ class for doing the work of handing out and tracking ports """
    def __init__(self, system_manager, debug = 0):
        # This is a file that can be read into a dictionary
        # it is in port:owner format
        self.skip_keys = [ 'port_file_delimiter'
                         , 'system_manager'
                         ]
        self.working_dir = "/tmp"
        self.file_prefix = "dbqp_port"
        self.port_file_delimiter = ':' # what we use to separate port:owner 
        self.debug = debug
        self.logging = system_manager.logging
        self.system_manager = system_manager
        self.logging.debug_class(self)
        

    def get_port_block(self, requester, base_port, block_size):
        """ Try to return a block of ports of size
            block_size, starting with base_port

            We take a target port and increment it
            until we find an unused port.  We make
            no guarantee of continuous ports, only
            that we will try to return block_size
            ports for use
    
            We can probably get fancier / smarter in the future
            but this should work for now :-/

        """
        assigned_ports = []
        current_port = base_port
        while len(assigned_ports) != block_size:
            new_port = (self.get_port(requester, current_port))
            assigned_ports.append(new_port)
            current_port = new_port+1
        return assigned_ports

    def get_port(self, requester, desired_port):
        """ Try to lock in the desired_port
            if not, we increment the value until
            we find an unused port.
            We take max / min port values from test-run.pl
            This is a bit bobo, but will work for now...

        """
        searching_for_port = 1
        attempt_count = 5000
        attempts_remain = attempt_count
        max_port_value = 32767
        min_port_value = 5001
        while searching_for_port and attempts_remain:
            # Check if the port is used
            if self.check_port_status(desired_port): 
                # assign it
                self.assign_port(requester, desired_port)
                return desired_port
            else: # increment the port and try again
                desired_port = desired_port + 1
                if desired_port >= max_port_value:
                    desired_port = min_port_value
                attempts_remain = attempts_remain - 1
        self.logging.error("Failed to assign a port in %d attempts" %attempt_count)
        sys.exit(1)

    def check_port_status(self, port):
        """ Check if a port is in use, via the port files
            which all copies of dbqp.py should use

            Not *really* sure how well this works with multiple
            dbqp.py instances...we'll see if we even need it 
            to work 

        """
        # check existing ports dbqp has created
        dbqp_ports = self.check_dbqp_ports()
        if port not in dbqp_ports and not self.is_port_used(port):
            return 1
        else:
            return 0

    def is_port_used(self, port):
        """ See if a given port is used on the system """
        retcode, output = self.system_manager.execute_cmd("netstat -lant")
        # parse our output
        entry_list = output.split("\n")
        good_data = 0
        for entry in entry_list:
            if entry.startswith('Proto'):
                good_data = 1
            elif good_data:
                # We try to catch additional output
                # like we see with freebsd
                if entry.startswith('Active'):
                    good_data = 0
                    pass
                else:
                    if self.system_manager.cur_os in [ 'FreeBSD' 
                                                     , 'Darwin' 
                                                     ]:
                        split_token = '.'
                    else:
                        split_token = ':'
                    port_candidate = entry.split()[3].split(split_token)[-1].strip()
                    if port_candidate.isdigit():
                        used_port = int(port_candidate)
                    else:
                        used_port = None # not a value we can use
                if port == used_port:
                    if entry.split()[-1] != "TIME_WAIT":
                        return 1
        return 0

    

    def check_dbqp_ports(self):
        """ Scan the files in /tmp for those files named
            dbqp_port_NNNN.  Existence indicates said port is 'locked'

        """
        used_ports = []
        tmp_files = os.listdir('/tmp')
        for tmp_file in tmp_files:
            if tmp_file.startswith('dbqp_port'):
                used_ports.append(int(tmp_file.split('_')[-1]))
        return used_ports

    def assign_port(self, owner, port):
        """Assigns a port - create a tmpfile
           with a name that 'logs' the port
           as being used

        """

        out_file = open(self.get_file_name(port),'w')
        out_file.write("%s:%d\n" %(owner, port))
        out_file.close()

    def free_ports(self, portlist):
       """ Clean up our ports """
       for port in portlist:
          self.free_port(port)

    def free_port(self, port):
       """ Free a single port - we delete the file
           that 'locks' it

       """
      
       self.logging.debug("Freeing port %d" %(port))
       try:
           os.remove(self.get_file_name(port))
       except OSError:
           pass

    def get_file_name(self, port):
        """ We generate a file name for the port """
      
        port_file_name = "%s_%s_%d" %(self.file_prefix, self.system_manager.cur_user, port )
        return os.path.join(self.working_dir, port_file_name)
        
       
       

