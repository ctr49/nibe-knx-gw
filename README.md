nibe-knx-gw
===========

Nibe RCU to interface with EIB/KNX


Prereqs:
libftdi
libeibclient

in absende of a proper build procedure just compile using:
gcc bcusdk-include/common.c nibe-rcu.c -lftdi -leibclient -o nibe-knx-gw
