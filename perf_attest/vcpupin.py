#!/usr/bin/python
# File Name: vcpupin.py.py

"""
	This file is used to pin the vm to a different physical core
"""

import os
import string
import sys 

import libvirt

VM_UUID = "38f332c5-a1cc-3fb1-1dc4-efcf1ee503bc"
USER_NAME = "palms_admin"
HOST_ADDR = "192.168.1.5"
NUM_CORE = 24


conn=libvirt.open("qemu+ssh://"+USER_NAME+"@"+HOST_ADDR+"/system")

dom0 = conn.lookupByUUIDString(VM_UUID)

for vcpu in range(len(dom0.vcpus()[0])):
	vcpu_id = dom0.vcpus()[0][vcpu][0]
	vcpu_location = list(dom0.vcpus()[1][vcpu])
	for i in range(len(vcpu_location)):
		if vcpu_location[i] == True:
			vcpu_location[i] = False
			vcpu_location[(i+1)%NUM_CORE] = True
			break
	dom0.pinVcpu(vcpu_id, tuple(vcpu_location))
