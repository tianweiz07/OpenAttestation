#!/usr/bin/python
# File Name: vcpupin.py.py

"""
	This file is used to pin the vm to a different physical core
"""

import os
import string
import sys 
import libvirt

NUM_CORE = 24

def vcpu_pin(uuid, host_addr, user_name):
    conn=libvirt.open("qemu+ssh://"+user_name+"@"+host_addr+"/system")
    dom0 = conn.lookupByUUIDString(uuid)

    for vcpu in range(len(dom0.vcpus()[0])):
        vcpu_id = dom0.vcpus()[0][vcpu][0]
        vcpu_location = list(dom0.vcpus()[1][vcpu])
        for i in range(len(vcpu_location)):
            if vcpu_location[i] == True:
                vcpu_location[i] = False
                vcpu_location[(i+1)%NUM_CORE] = True
                break
        dom0.pinVcpu(vcpu_id, tuple(vcpu_location))
