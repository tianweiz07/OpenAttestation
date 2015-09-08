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

def vm_migrate(uuid, source_host_addr, source_user_name, dest_host_addr, dest_user_name):
    conn=libvirt.open("qemu+ssh://"+source_user_name+"@"+source_host_addr+"/system")
    conn1=libvirt.open("qemu+ssh://"+dest_user_name+"@"+dest_user_addr+"/system")

    dom0 = conn.lookupByUUIDString(uuid)

    threads = []
    t1 = threading.Thread(target=dom0.migrate, args=(conn1, 1, None, None, 0))
    t2 = threading.Thread(target=dom0.migrateSetMaxDowntime, args=(1000, 0))
    threads.append(t1)
    threads.append(t2)
    t1.start()
    time.sleep(5)
    t2.start()
    while (t1.isAlive() or t2.isAlive()):
        pass
