#!/usr/bin/python
#
#    This file is used to send attestation requests to the compute node periodically based on the customer's requests.
#    Every miniute it will check the database and update the customer's request. The unit of the time interval for the customer's requests should be miniute.
#    Execute ./oat_cert -h <Attestation Client> first to generate the <Attestation Client>_certfile.cer. Then put the <Attestation Client>_certfile.cer into the same directory as this script.
#    The actual command is: curl --cacert certfile.cer -H "Content-Type: application/json" -X POST -d '{"hosts":["nebula1"]}' "https://nebula1:8443/AttestationService/resources/PollHosts" -ssl3
#
#

"""
Trust Monitor
"""

import os
import time
import sys
import threading
import MySQLdb
from math import log

OAT_SERVER='nebula6'
CERT_FILE = '/root/OpenAttestation/CommandTool/certfile.cer'

ATTEST_HOST = "nebula1"
ATTEST_VM = "38f332c5-a1cc-3fb1-1dc4-efcf1ee503bc"
REFERENCE_HOST = "nebula2"
REFERENCE_VM = "19c1125f-6719-679b-309e-0685cf803041"
EVENT_MASK = "1e7"

def connect_db(table):
    db = MySQLdb.connect(host="localhost", port = 3306, user= "root", passwd="adminj310a", db="oat_db")
    sql = "SELECT * FROM " + table
    cursor = db.cursor()
    cursor.execute(sql)
    results = cursor.fetchall()
    cursor.close()
    db.close()
    return results

# curl -k --cacert certfile.cer -H "Content-Type: application/json" -X POST -d '{"hosts":["nebula1"],"property":["1"],"id":["38f332c5-a1cc-3fb1-1dc4-efcf1ee503bc"]}' "https://nebula6:8443/AttestationService/resources/PollHosts"

def execute_cmd(machine, vm_id, security_property):
    machine_info = '["' + machine + '"]'
    vm_info = '["' + vm_id + '"]'
    property_info = '["' + security_property + '"]'

    cmd = 'curl -k --cacert ' + CERT_FILE + ' -H "Content-Type: application/json" ' + '-X POST' + ' -d' + """ '{"hosts":""" + machine_info + ', "id":' + vm_info + ', "property":'+ property_info + "}' " + '"https://' + OAT_SERVER + ':8443' + '/AttestationService/resources/PollHosts"'
    result = os.popen(cmd)
    return result.read()

def get_entry(host_name):
    results = connect_db("audit_log")
    result1 = [row for row in results if row[2].lower() == host_name]
    return result1[-1]

def get_value(event, entry):
    if event == "instructions":
        index = 4
    if event == "L1-dcache-loads":
        index = 5
    if event == "L1-dcache-stores":
        index = 11
    if event == "L1-icache-loads":
        index = 12
    if event == "L1-icache-stores":
        index = 6
    if event == "LLC-loads":
        index = 7
    if event == "LLC-stores":
        index = 13
    if event == "LLC-load-misses":
        index = 14
    if event == "LLC-store-misses":
        index = 15

    value_str=""
    for i in range(16):
        value_str += entry[index][15-i]
    return int(value_str)



def security_attest():
    threads = []
    t1 = threading.Thread(target=execute_cmd, args=(ATTEST_HOST, ATTEST_VM, EVENT_MASK))
    t2 = threading.Thread(target=execute_cmd, args=(REFERENCE_HOST, REFERENCE_VM, EVENT_MASK))
    threads.append(t1)
    threads.append(t2)
    t1.start()
    t2.start()
    while (t1.isAlive() or t2.isAlive()):
        pass

def instr_ratio():
    REFERENCE_instr = get_value("instructions", get_entry(REFERENCE_HOST))
    ATTEST_instr = get_value("instructions", get_entry(ATTEST_HOST))
    return float(REFERENCE_instr)/float(ATTEST_instr)

def llc_kl():
    REFERENCE_llc_loads = get_value("LLC-loads", get_entry(REFERENCE_HOST))
    REFERENCE_llc_stores = get_value("LLC-stores", get_entry(REFERENCE_HOST))
    REFERENCE_llc_load_misses = get_value("LLC-load-misses", get_entry(REFERENCE_HOST))
    REFERENCE_llc_store_misses = get_value("LLC-store-misses", get_entry(REFERENCE_HOST))
    ATTEST_llc_loads = get_value("LLC-loads", get_entry(ATTEST_HOST))
    ATTEST_llc_stores = get_value("LLC-stores", get_entry(ATTEST_HOST))
    ATTEST_llc_load_misses = get_value("LLC-load-misses", get_entry(ATTEST_HOST))
    ATTEST_llc_store_misses = get_value("LLC-store-misses", get_entry(ATTEST_HOST))

    REFERENCE_p = (float(REFERENCE_llc_load_misses)+float(REFERENCE_llc_store_misses))/(float(REFERENCE_llc_loads)+float(REFERENCE_llc_stores))
    ATTEST_p = (float(ATTEST_llc_load_misses)+float(ATTEST_llc_store_misses))/(float(ATTEST_llc_loads)+float(ATTEST_llc_stores))

    if (REFERENCE_p > 1 or ATTEST_p > 1):
        LLC_KL = 0
    else:
        LLC_KL = REFERENCE_p*log(REFERENCE_p/ATTEST_p) + (1-REFERENCE_p)*log((1-REFERENCE_p)/(1-ATTEST_p))

    return LLC_KL

def bus_kl():
    REFERENCE_l1d_loads = get_value("L1-dcache-loads", get_entry(REFERENCE_HOST))
    REFERENCE_l1d_stores = get_value("L1-dcache-stores", get_entry(REFERENCE_HOST))
    REFERENCE_l1i_loads = get_value("L1-icache-loads", get_entry(REFERENCE_HOST))
    REFERENCE_l1i_stores = get_value("L1-icache-stores", get_entry(REFERENCE_HOST))
    ATTEST_l1d_loads = get_value("L1-dcache-loads", get_entry(ATTEST_HOST))
    ATTEST_l1d_stores = get_value("L1-dcache-stores", get_entry(ATTEST_HOST))
    ATTEST_l1i_loads = get_value("L1-icache-loads", get_entry(ATTEST_HOST))
    ATTEST_l1i_stores = get_value("L1-icache-stores", get_entry(ATTEST_HOST))

    REFERENCE_p = (float(REFERENCE_l1d_loads) + float(REFERENCE_l1d_stores) + float(REFERENCE_l1i_loads) + float(REFERENCE_l1i_stores))
    ATTEST_p = (float(ATTEST_l1d_loads) + float(ATTEST_l1d_stores) + float(ATTEST_l1i_loads) + float(ATTEST_l1i_stores))
    BUS_KL = log(REFERENCE_p/ATTEST_p)
    return BUS_KL

security_attest()
print instr_ratio()
print llc_kl()
print bus_kl()
