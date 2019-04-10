MPI=yes
CRAY=yes

CC=mpicc
ifeq ($(MPI),no)
CC=gcc
endif

CFLAGS=-O3 -D_MPI -g -I../../include
ifeq ($(JLSE),yes)
CFLAGS+= -fopenmp
endif
ifeq ($(CRAY),yes)
CC=mpicc
CFLAGS+= -qopenmp
endif
ifeq ($(BGQ),yes)
CC=mpixlc
CFLAGS+= -qsmp=omp -qthreaded
endif

POLILIB=-L../../lib/ ../../lib/libpolimer.so
LDFLAGS=

all:
	$(CC) $(CFLAGS) mpi_power_demo.c -o mpi_power_demo $(LDFLAGS) $(POLILIB)

clean:
	rm mpi_power_demo
