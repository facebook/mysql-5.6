#! /usr/bin/env python
# -*- mode: python; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
#
# Copyright (C) 2010 Patrick Crews
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

"""time_management.py
   code for dealing with timing, mostly
   we want to keep track of how long certain things
   take, so we do that here

"""

# imports
import time

class timeRecord:
    """ data structure to keep track of execution times """

    def __init__(self, start_time, type):
        self.start_time = start_time
        self.stop_time = None
        self.elapsed_time = None
        self.type = type

    def set_stop_time(self, stop_time):
        self.stop_time = stop_time
        self.elapsed_time = self.stop_time - self.start_time

class timeManager:
    """ keeps track of the elapsed time for various events
        and provides access to this information as needed

    """

    def __init__(self, system_manager):
        self.skip_keys = [ 'system_manager'
                         ]
        self.debug = system_manager.debug
        self.verbose = system_manager.verbose
        self.system_manager = system_manager
        self.logging = system_manager.logging
        self.time_records = {}
  
    def start(self, key_value, type):
        """ We create a new record to keep track of
            with the key being key_value

            We track type as a convenience.  Not
            sure how useful it will be / long it
            will live

        """

        start_time = time.time()
        self.time_records[key_value] = timeRecord(start_time, type)

    def stop(self, key_value):
        """ We stop timing for key_value, and return elapsed time """
        stop_time = time.time()
        self.time_records[key_value].set_stop_time(stop_time)
        return self.time_records[key_value].elapsed_time  

    def summary_report(self):
        """ We do a bit o' reporting """
        records = self.sort_records_by_type()
        total_time = self.time_records['total_time'].elapsed_time
        for record_type in records.keys():
            if record_type != 'total_time': # skip this it would be silly
                type_total_time = self.get_total_time(records[record_type])
                self.logging.info("Spent %d / %d seconds on: %s(s)" % ( type_total_time
                                                                        , total_time
                                                                        , record_type.upper()
                                                                        ))

    def sort_records_by_type(self):
        """ We go through our records and return a dictionary
            with keys = 'type', and a list of timeRecords with that type
            as the data

        """
        sorted_records = {}
        for time_record in self.time_records.values():
            if time_record.type not in sorted_records:
                sorted_records[time_record.type] = [time_record]
            else:
                sorted_records[time_record.type].append(time_record)
        return sorted_records

    def get_total_time(self, record_list):
        """ We return the total elapsed time for
            all of the records in our record list
 
        """
       
        total_elapsed_time = 0
        for record in record_list:
            total_elapsed_time = total_elapsed_time + record.elapsed_time
        return total_elapsed_time
         
        

        
