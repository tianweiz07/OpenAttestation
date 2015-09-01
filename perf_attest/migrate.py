#!/usr/bin/python
# File Name: vcpupin.py.py

"""
	This file is used to pin the vm to a different physical core
"""

import os
import string
import sys 
import threading
import time
import libvirt

VM_UUID = "38f332c5-a1cc-3fb1-1dc4-efcf1ee503bc"
SOURCE_USER_NAME = "palms_admin"
SOURCE_HOST_ADDR = "192.168.1.16"
DEST_USER_NAME = "palms_admin"
DEST_HOST_ADDR = "192.168.1.5"

conn=libvirt.open("qemu+ssh://"+SOURCE_USER_NAME+"@"+SOURCE_HOST_ADDR+"/system")
conn1=libvirt.open("qemu+ssh://"+DEST_USER_NAME+"@"+DEST_HOST_ADDR+"/system")

dom0 = conn.lookupByUUIDString(VM_UUID)

threads = []
t1 = threading.Thread(target=dom0.migrate, args=(conn1, 1, None, None, 0))
t2 = threading.Thread(target=dom0.migrateSetMaxDowntime, args=(1000, 0))
threads.append(t1)
threads.append(t2)
t1.start()
time.sleep(5)
t2.start()


