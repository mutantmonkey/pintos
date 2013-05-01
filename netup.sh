#!/bin/sh
#ip tuntap add dev tap1 mode tap
brctl addbr br0
brctl addif br0 tap0
brctl addif br0 enp0s25
ip addr add 10.177.0.1/24 dev br0
ip link set tap0 up
ip link set enp0s25 up
ip link set br0 up
