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
#import MySQLdb
import getopt
import locale
import argparse
import vcpupin
import migrate
from math import log

OAT_SERVER='nebula6'
CERT_FILE = '/root/OpenAttestation/CommandTool/certfile.cer'

ATTEST_HOST = "nebula1"
ATTEST_VM = "38f332c5-a1cc-3fb1-1dc4-efcf1ee503bc"
REFERENCE_HOST = "nebula2"
REFERENCE_VM = "19c1125f-6719-679b-309e-0685cf803041"
EVENT_MASK = "1"

USER_NAME = "palms_admin"
DEST_HOST = "nebula2"

MAX_ALARM = 4
KS_THRESHOLD = 0.275
INTERVAL = 15


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

def get_value(prob_index, entry):
    if prob_index == 0:
        index = 4
    if prob_index == 1:
        index = 5
    if prob_index == 2:
        index = 11
    if prob_index == 3:
        index = 12
    if prob_index == 4:
        index = 6
    if prob_index == 5:
        index = 7
    if prob_index == 6:
        index = 13
    if prob_index == 7:
        index = 14
    if prob_index == 8:
        index = 15
    if prob_index == 9:
        index = 16

    value_str=""
    for i in range(7):
        value_str += entry[index][6-i]
    return float(value_str)/1000000

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

def KS_calculate():
    D = 0.0
    for i in range(10):
        pro1 = get_value(i, get_entry(REFERENCE_HOST))
        pro2 = get_value(i, get_entry(ATTEST_HOST))
        D = max(D, pro1-pro2, pro2-pro1)
    return D

def main(argv):
    alarm_num = 0
    while(0):
	start_time = time.time()
        security_attest()
        D = KS_calculate()
        if (D > KS_THRESHOLD):
            alarm_num = alarm_num + 1
            if (alarm_num == MAX_ALARM):
                alarm_num = 0
                if (EVENT_MASK == "4"):
		    vcpupin.vcpu_pin(ATTEST_VM, ATTEST_HOST, USER_NAME)
                if (EVENT_MASK == "1"):
                    migrate.vm_migrate(ATTEST_VM, ATTEST_HOST, USER_NAME, DEST_HOST, USERNAME)
	else:
		alarm_num = 0
        while (time.time() - start_time < 15):
		pass

if __name__ == "__main__":
    main(sys.argv[1:])
