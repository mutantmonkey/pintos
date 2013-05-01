#!/bin/sh
ip link set tap0 down
ip link set enp0s25 down
ip link set br0 down
brctl delbr br0
#brctl delif br0 tap0
brctl delif br0 enp0s25
#ip tuntap del dev tap1 mode tap
