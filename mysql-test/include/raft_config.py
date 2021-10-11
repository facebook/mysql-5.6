#!/usr/local/bin/python3

import os
import sys
import json
import socket
import re

def execute_sql(server_id, query):
    socket = f'{socket_path}/mysqld.{server_id}.sock'
    stream = os.popen(
        f'mysql --no-defaults -S {socket} -u root -D test -sNe "{query}"'
    )
    return stream.read().strip()

def ip():
    stream = os.popen(
        f'hostname -i'
    )
    return stream.read().strip()

num_servers = int(sys.argv[1])
socket_path = sys.argv[2]

hostname = socket.gethostname()
hostname = hostname.replace('.facebook.com', '')
dc = hostname.split(".")[-1]
region = re.sub('[0-9]+$', '', dc)
ip = ip()

config={}

for i in range(1, num_servers + 1):
    uuid = execute_sql(i, "select @@global.server_uuid")
    server_id = int(execute_sql(i, "select @@global.server_id"))
    port = execute_sql(i, "select @@global.port")

    config["bootstrap"] = True
    config["commit_rule"] = {"mode": 2}
    config["replicaset_name"] = "mysql.replicaset.0"
    server_config = {}
    server_config["region"] = region
    server_config["hostname"] = hostname
    server_config["ip_port"] = f"[{ip}]:{port}"
    server_config["uuid"] = uuid
    server_config["backed_by_database"] = True
    server_config["voter_type"] = 0
    server_config["server_id"] = server_id
    config["server_config"] = server_config

    server_props = []

    for j in range(1, num_servers + 1):
        uuid = execute_sql(j, "select @@global.server_uuid")
        server_id = int(execute_sql(j, "select @@global.server_id"))
        port = execute_sql(j, "select @@global.port")
        server_prop = {}
        server_prop["region"] = region
        server_prop["hostname"] = hostname
        server_prop["ip_port"] = f"[{ip}]:{port}"
        server_prop["uuid"] = uuid
        server_prop["server_id"] = server_id
        server_prop["backed_by_database"] = True
        server_prop["voter_type"] = 0
        server_props.append(server_prop)

    config["raft_topology"] = {"raft_server_properties": server_props}
    config["voter_distribution"] = {region: num_servers}
    config["enable_flexiraft"] = False

    config_str = json.dumps(config)
    config_str = config_str.replace('"', r'\"')
    execute_sql(i, f"set @@global.rpl_raft_topology_config_json='{config_str}'")
