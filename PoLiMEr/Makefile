MSR=yes
POWMGR=yes

CFLAGS=-I. -O3 -g
Q_AR=ar
CC=`which cc`
LIB=
#JLSE
ifeq ($(JLSE),yes)
CFLAGS+=-D_COBALT
ifneq ($(NOMPI),yes)
CC=`which mpicc`
endif
ifneq ($(NOOMP),yes)
CFLAGS+=-fopenmp
endif
endif

#use MSRs/RAPL
ifeq ($(MSR),yes)
CFLAGS+=-D_MSR
ifeq ($(POWMGR),yes)
CFLAGS+=-D_POWMGR
endif
endif

#CRAY
ifeq ($(CRAY), yes)
CFLAGS+=-D_PMI -D_CRAY #-I${PMILOC}/include
PMILOC=/opt/cray/pe/pmi/default
#LIB+=-L${PMILOC}/lib64 ${PMILOC}/lib64/libpmi.a
ifneq ($(NOOMP),yes)
CFLAGS+=-qopenmp
endif
endif

#BGQ
ifeq ($(BGQ),yes)
CC=mpixlc
ifneq ($(NOOMP),yes)
CFLAGS+= -qsmp=omp -qthreaded 
endif
CFLAGS+=-D_BGQ -qpic
Q_AR = /bgsys/drivers/ppcfloor/gnu-linux/bin/powerpc64-bgq-linux-ar
else
CFLAGS+=-fPIC
endif

#other flags
ifeq ($(DEBUG),yes)
CFLAGS+=-D_DEBUG
endif
ifeq ($(TRACE),yes)
CFLAGS+=-D_TRACE
endif
ifeq ($(TIMER_OFF),yes)
CFLAGS+=-D_TIMER_OFF
endif
ifeq ($(BENCH),yes)
CFLAGS+=-D_BENCH
endif
ifeq ($(NOMPI),yes)
CFLAGS+=-D_NOMPI
endif
ifeq ($(NOOMP),yes)
CFLAGS+=-D_NOOMP
endif
ifeq ($(COBALT),yes)
CFLAGS+=-D_COBALT
endif

CFLAGS+=-I./include

LIBDIR=lib
OBJDIR=bin

ifeq ($(TIMER_OFF),yes)
LIBDIR=lib_notimer
OBJDIR=bin_notimer
endif

all: $(LIBDIR)/libpolimer.a $(LIBDIR)/libpolimer.so
notimer: $(LIBDIR)/libpolimer_notimer.a $(LIBDIR)/libpolimer_notimer.so

OBJ = $(OBJDIR)/PoLiMEr.o $(OBJDIR)/PoLiLog.o $(OBJDIR)/output.o $(OBJDIR)/frequency_handler.o $(OBJDIR)/helpers.o

ifneq ($(NOMPI),yes)
OBJ+= $(OBJDIR)/mpi_handler.o
endif

ifeq ($(MSR),yes)
OBJ+= $(OBJDIR)/msr_handler.o $(OBJDIR)/power_cap_handler.o
ifeq ($(POWMGR),yes)
OBJ+= $(OBJDIR)/power_manager.o
endif
endif

ifeq ($(CRAY),yes)
OBJ+= $(OBJDIR)/cray_handler.o
endif

ifeq ($(BGQ),yes)
OBJ+= $(OBJDIR)/bgq_handler.o
endif

$(OBJ): $(OBJDIR)/%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@ $(LIB)

$(LIBDIR)/libpolimer.a: $(OBJ)
	$(Q_AR) rcs $@ $(OBJ)

$(LIBDIR)/libpolimer.so: $(OBJ)
	`which cc` -shared -o $@ $(OBJ)

clean:
	rm -f lib/*.a bin/*.o lib/*.so a.out
clean-notimer:
	rm -f lib_notimer/*.a bin_notimer/*.o lib_notimer/*.so a.out
