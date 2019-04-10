#!/bin/bash
export CRAYPE_LINK_TYPE=dynamic
if [ ! -d lib_notimer ]; then
	makedir lib_notimer
fi
if [ ! -d bin_notimer ]; then
	makedir bin_notimer
fi
makedir bin_notimer
make clean-notimer
make notimer CRAY=yes COBALT=yes NOOMP=yes TIMER_OFF=yes
