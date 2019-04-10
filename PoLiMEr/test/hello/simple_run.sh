#!/bin/bash
#COBALT -n 1 -q debug-cache-quad -t 60 -O simple_run

export PoLi_PREFIX="simple_run_"

rpn=8
th=2

VTUNE=/opt/intel/vtune_amplifier_2018.0.2.525261
source ${VTUNE}/amplxe-vars.sh

aprun --env LD_LIBRARY_PATH=$LD_LIBRARY_PATH:${VTUNE}/lib64 \
	--env PATH=${PATH}:${VTUNE}/bin64 \
	--env PMI_NO_FORK=1 \
	-n $((COBALT_PARTSIZE*rpn)) -N $rpn -d $th -j $th --cc depth \
	amplxe-cl -collect memory-consumption -data-limit=0 -result-dir vtune_res ./mpi_power_demo $th
