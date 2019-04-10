#!/bin/bash
export CRAYPE_LINK_TYPE=dynamic
make clean
make CRAY=yes COBALT=yes NOOMP=yes
