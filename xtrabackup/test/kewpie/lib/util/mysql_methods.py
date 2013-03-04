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

""" mysql_methods
    collection of helper methods (mysqldump, execute_query, etc)
    that make working with a given server easier and repeatable

"""

import os
import difflib
import subprocess

import MySQLdb

def execute_cmd(cmd, stdout_path, exec_path=None, get_output=False):
    stdout_file = open(stdout_path,'w')
    cmd_subproc = subprocess.Popen( cmd
                                  , shell=True
                                  , cwd=exec_path
                                  , stdout = stdout_file
                                  , stderr = subprocess.STDOUT
                                  )
    cmd_subproc.wait()
    retcode = cmd_subproc.returncode
    stdout_file.close()
    if get_output:
        data_file = open(stdout_path,'r')
        output = ''.join(data_file.readlines())
    else:
        output = None
    return retcode, output

def get_tables(server, schema):
    """ Return a list of the tables in the
        schema on the server

    """
    results = []
    query = "SHOW TABLES IN %s" %(schema)
    retcode, table_set = execute_query(query, server)
    for table_data in table_set:
        table_name = table_data[0]
        results.append(table_name)
    return results

def check_slaves_by_query( master_server
                         , other_servers
                         , query
                         , expected_result = None
                         ):
    """ We execute the query across all servers
        and return a dict listing any diffs found,
        None if all is good.

        If a user provides an expected_result, we
        will skip executing against the master
        This is done as it is assumed the expected
        result has been generated / tested against
        the master

    """
    comp_results = {}
    if expected_result:
        pass # don't bother getting it
    else:
        # run against master for 'good' value
        retcode, expected_result = execute_query(query, master_server)
    for server in other_servers:
        retcode, slave_result = execute_query(query, server)
        #print "%s: expected_result= %s | slave_result= %s" % ( server.name 
        #                                                     , expected_result 
        #                                                     , slave_result_
        #                                                       )

        if not expected_result == slave_result:
            comp_data = "%s: expected_result= %s | slave_result= %s" % ( server.name 
                                                                       , expected_result 
                                                                       , slave_result
                                                                       )
            if comp_results.has_key(server.name):
                comp_results[server.name].append(comp_data)
            else:
                comp_results[server.name]=[comp_data]
    if comp_results:
        return comp_results
    return None
 

def check_slaves_by_checksum( master_server
                            , other_servers
                            , schemas=['test']
                            , tables=[]
                            ):
    """ We compare the specified tables (default = all)
        from the specified schemas between the 'master'
        and the other servers provided (via list)
        via CHECKSUM

        We return a dictionary listing the server
        and any tables that differed

    """
    comp_results = {}
    for server in other_servers:
        for schema in schemas:
            for table in get_tables(master_server, schema):
                query = "CHECKSUM TABLE %s.%s" %(schema, table)
                retcode, master_checksum = execute_query(query, master_server)
                retcode, slave_checksum = execute_query(query, server)
                #print "%s: master_checksum= %s | slave_checksum= %s" % ( table
                #                                                       , master_checksum
                #                                                       , slave_checksum
                #                                                       )

                if not master_checksum == slave_checksum:
                    comp_data = "%s: master_checksum= %s | slave_checksum= %s" % ( table
                                                                                 , master_checksum
                                                                                 , slave_checksum
                                                                                 )
                    if comp_results.has_key(server.name):
                        comp_results[server.name].append(comp_data)
                    else:
                        comp_results[server.name]=[comp_data]
    if comp_results:
        return comp_results
    return None 



def take_mysqldump( server
                  , databases=[]
                  , tables=[]
                  , dump_path = None
                  , cmd_root = None):
    """ Take a mysqldump snapshot of the given
        server, storing the output to dump_path

    """
    if not dump_path:
        dump_path = os.path.join(server.vardir, 'dumpfile.dat')

    if cmd_root:
        dump_cmd = cmd_root
    else:
        dump_cmd = "%s --no-defaults --user=root --port=%d --host=127.0.0.1 --protocol=tcp --result-file=%s" % ( server.mysqldump
                                                                                                            , server.master_port
                                                                                                            , dump_path
                                                                                                            )
        if databases:
            if len(databases) > 1:
                # We have a list of db's that are to be dumped so we handle things
                dump_cmd = ' '.join([dump_cmd, '--databases', ' '.join(databases)])
            else:
               dump_cmd = ' '.join([dump_cmd, databases[0], ' '.join(tables)])

    execute_cmd(dump_cmd, os.devnull)


def diff_dumpfiles(orig_file_path, new_file_path):
    """ diff two dumpfiles useful for comparing servers """ 
    orig_file = open(orig_file_path,'r')
    restored_file = open(new_file_path,'r')
    orig_file_data = []
    rest_file_data = []
    orig_file_data= filter_data(orig_file.readlines(),'Dump completed')
    rest_file_data= filter_data(restored_file.readlines(),'Dump completed') 
    
    server_diff = difflib.unified_diff( orig_file_data
                                      , rest_file_data
                                      , fromfile=orig_file_path
                                      , tofile=new_file_path
                                      )
    diff_output = []
    for line in server_diff:
        diff_output.append(line)
    output = '\n'.join(diff_output)
    orig_file.close()
    restored_file.close()
    return (diff_output==[]), output

def filter_data(input_data, filter_text ):
    return_data = []
    for line in input_data:
        if filter_text in line.strip():
            pass
        else:
            return_data.append(line)
    return return_data

def execute_query( query
                 , server
                 , server_host = '127.0.0.1'
                 , schema='test'):
    try:
        conn = MySQLdb.connect( host = server_host
                              , port = server.master_port
                              , user = 'root'
                              , db = schema)
        cursor = conn.cursor()
        cursor.execute(query)
        result_set =  cursor.fetchall()
        cursor.close()
    except MySQLdb.Error, e:
        return 1, ("Error %d: %s" %(e.args[0], e.args[1]))
    conn.commit()
    conn.close()
    return 0, result_set

def execute_queries( query_list
                   , server
                   , server_host= '127.0.0.1'
                   , schema= 'test'):
    """ Execute a set of queries as a single transaction """

    results = {} 
    retcode = 0
    try:
        conn = MySQLdb.connect( host = server_host
                              , port = server.master_port
                              , user = 'root'
                              , db = schema)
        cursor = conn.cursor()
        for idx, query in enumerate(query_list):
            try:
                cursor.execute(query)
                result_set = cursor.fetchall()
            except MySQLdb.Error, e:
                result_set = "Error %d: %s" %(e.args[0], e.args[1])   
                retcode = 1
            finally:
                results[query+str(idx)] = result_set
        conn.commit()
        cursor.close()
        conn.close()
    except Exception, e:
        retcode = 1
        results = (Exception, e)
    finally:
        return retcode, results    
